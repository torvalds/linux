//===-- RenderScriptRuntime.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringSwitch.h"

#include "RenderScriptRuntime.h"
#include "RenderScriptScriptGroup.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_renderscript;

#define FMT_COORD "(%" PRIu32 ", %" PRIu32 ", %" PRIu32 ")"

namespace {

// The empirical_type adds a basic level of validation to arbitrary data
// allowing us to track if data has been discovered and stored or not. An
// empirical_type will be marked as valid only if it has been explicitly
// assigned to.
template <typename type_t> class empirical_type {
public:
  // Ctor. Contents is invalid when constructed.
  empirical_type() : valid(false) {}

  // Return true and copy contents to out if valid, else return false.
  bool get(type_t &out) const {
    if (valid)
      out = data;
    return valid;
  }

  // Return a pointer to the contents or nullptr if it was not valid.
  const type_t *get() const { return valid ? &data : nullptr; }

  // Assign data explicitly.
  void set(const type_t in) {
    data = in;
    valid = true;
  }

  // Mark contents as invalid.
  void invalidate() { valid = false; }

  // Returns true if this type contains valid data.
  bool isValid() const { return valid; }

  // Assignment operator.
  empirical_type<type_t> &operator=(const type_t in) {
    set(in);
    return *this;
  }

  // Dereference operator returns contents.
  // Warning: Will assert if not valid so use only when you know data is valid.
  const type_t &operator*() const {
    assert(valid);
    return data;
  }

protected:
  bool valid;
  type_t data;
};

// ArgItem is used by the GetArgs() function when reading function arguments
// from the target.
struct ArgItem {
  enum { ePointer, eInt32, eInt64, eLong, eBool } type;

  uint64_t value;

  explicit operator uint64_t() const { return value; }
};

// Context structure to be passed into GetArgsXXX(), argument reading functions
// below.
struct GetArgsCtx {
  RegisterContext *reg_ctx;
  Process *process;
};

bool GetArgsX86(const GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  Status err;

  // get the current stack pointer
  uint64_t sp = ctx.reg_ctx->GetSP();

  for (size_t i = 0; i < num_args; ++i) {
    ArgItem &arg = arg_list[i];
    // advance up the stack by one argument
    sp += sizeof(uint32_t);
    // get the argument type size
    size_t arg_size = sizeof(uint32_t);
    // read the argument from memory
    arg.value = 0;
    Status err;
    size_t read =
        ctx.process->ReadMemory(sp, &arg.value, sizeof(uint32_t), err);
    if (read != arg_size || !err.Success()) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64 " '%s'",
                    __FUNCTION__, uint64_t(i), err.AsCString());
      return false;
    }
  }
  return true;
}

bool GetArgsX86_64(GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  // number of arguments passed in registers
  static const uint32_t args_in_reg = 6;
  // register passing order
  static const std::array<const char *, args_in_reg> reg_names{
      {"rdi", "rsi", "rdx", "rcx", "r8", "r9"}};
  // argument type to size mapping
  static const std::array<size_t, 5> arg_size{{
      8, // ePointer,
      4, // eInt32,
      8, // eInt64,
      8, // eLong,
      4, // eBool,
  }};

  Status err;

  // get the current stack pointer
  uint64_t sp = ctx.reg_ctx->GetSP();
  // step over the return address
  sp += sizeof(uint64_t);

  // check the stack alignment was correct (16 byte aligned)
  if ((sp & 0xf) != 0x0) {
    if (log)
      log->Printf("%s - stack misaligned", __FUNCTION__);
    return false;
  }

  // find the start of arguments on the stack
  uint64_t sp_offset = 0;
  for (uint32_t i = args_in_reg; i < num_args; ++i) {
    sp_offset += arg_size[arg_list[i].type];
  }
  // round up to multiple of 16
  sp_offset = (sp_offset + 0xf) & 0xf;
  sp += sp_offset;

  for (size_t i = 0; i < num_args; ++i) {
    bool success = false;
    ArgItem &arg = arg_list[i];
    // arguments passed in registers
    if (i < args_in_reg) {
      const RegisterInfo *reg =
          ctx.reg_ctx->GetRegisterInfoByName(reg_names[i]);
      RegisterValue reg_val;
      if (ctx.reg_ctx->ReadRegister(reg, reg_val))
        arg.value = reg_val.GetAsUInt64(0, &success);
    }
    // arguments passed on the stack
    else {
      // get the argument type size
      const size_t size = arg_size[arg_list[i].type];
      // read the argument from memory
      arg.value = 0;
      // note: due to little endian layout reading 4 or 8 bytes will give the
      // correct value.
      size_t read = ctx.process->ReadMemory(sp, &arg.value, size, err);
      success = (err.Success() && read == size);
      // advance past this argument
      sp -= size;
    }
    // fail if we couldn't read this argument
    if (!success) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64 ", reason: %s",
                    __FUNCTION__, uint64_t(i), err.AsCString("n/a"));
      return false;
    }
  }
  return true;
}

bool GetArgsArm(GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  // number of arguments passed in registers
  static const uint32_t args_in_reg = 4;

  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  Status err;

  // get the current stack pointer
  uint64_t sp = ctx.reg_ctx->GetSP();

  for (size_t i = 0; i < num_args; ++i) {
    bool success = false;
    ArgItem &arg = arg_list[i];
    // arguments passed in registers
    if (i < args_in_reg) {
      const RegisterInfo *reg = ctx.reg_ctx->GetRegisterInfoAtIndex(i);
      RegisterValue reg_val;
      if (ctx.reg_ctx->ReadRegister(reg, reg_val))
        arg.value = reg_val.GetAsUInt32(0, &success);
    }
    // arguments passed on the stack
    else {
      // get the argument type size
      const size_t arg_size = sizeof(uint32_t);
      // clear all 64bits
      arg.value = 0;
      // read this argument from memory
      size_t bytes_read =
          ctx.process->ReadMemory(sp, &arg.value, arg_size, err);
      success = (err.Success() && bytes_read == arg_size);
      // advance the stack pointer
      sp += sizeof(uint32_t);
    }
    // fail if we couldn't read this argument
    if (!success) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64 ", reason: %s",
                    __FUNCTION__, uint64_t(i), err.AsCString("n/a"));
      return false;
    }
  }
  return true;
}

bool GetArgsAarch64(GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  // number of arguments passed in registers
  static const uint32_t args_in_reg = 8;

  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  for (size_t i = 0; i < num_args; ++i) {
    bool success = false;
    ArgItem &arg = arg_list[i];
    // arguments passed in registers
    if (i < args_in_reg) {
      const RegisterInfo *reg = ctx.reg_ctx->GetRegisterInfoAtIndex(i);
      RegisterValue reg_val;
      if (ctx.reg_ctx->ReadRegister(reg, reg_val))
        arg.value = reg_val.GetAsUInt64(0, &success);
    }
    // arguments passed on the stack
    else {
      if (log)
        log->Printf("%s - reading arguments spilled to stack not implemented",
                    __FUNCTION__);
    }
    // fail if we couldn't read this argument
    if (!success) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64, __FUNCTION__,
                    uint64_t(i));
      return false;
    }
  }
  return true;
}

bool GetArgsMipsel(GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  // number of arguments passed in registers
  static const uint32_t args_in_reg = 4;
  // register file offset to first argument
  static const uint32_t reg_offset = 4;

  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  Status err;

  // find offset to arguments on the stack (+16 to skip over a0-a3 shadow
  // space)
  uint64_t sp = ctx.reg_ctx->GetSP() + 16;

  for (size_t i = 0; i < num_args; ++i) {
    bool success = false;
    ArgItem &arg = arg_list[i];
    // arguments passed in registers
    if (i < args_in_reg) {
      const RegisterInfo *reg =
          ctx.reg_ctx->GetRegisterInfoAtIndex(i + reg_offset);
      RegisterValue reg_val;
      if (ctx.reg_ctx->ReadRegister(reg, reg_val))
        arg.value = reg_val.GetAsUInt64(0, &success);
    }
    // arguments passed on the stack
    else {
      const size_t arg_size = sizeof(uint32_t);
      arg.value = 0;
      size_t bytes_read =
          ctx.process->ReadMemory(sp, &arg.value, arg_size, err);
      success = (err.Success() && bytes_read == arg_size);
      // advance the stack pointer
      sp += arg_size;
    }
    // fail if we couldn't read this argument
    if (!success) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64 ", reason: %s",
                    __FUNCTION__, uint64_t(i), err.AsCString("n/a"));
      return false;
    }
  }
  return true;
}

bool GetArgsMips64el(GetArgsCtx &ctx, ArgItem *arg_list, size_t num_args) {
  // number of arguments passed in registers
  static const uint32_t args_in_reg = 8;
  // register file offset to first argument
  static const uint32_t reg_offset = 4;

  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  Status err;

  // get the current stack pointer
  uint64_t sp = ctx.reg_ctx->GetSP();

  for (size_t i = 0; i < num_args; ++i) {
    bool success = false;
    ArgItem &arg = arg_list[i];
    // arguments passed in registers
    if (i < args_in_reg) {
      const RegisterInfo *reg =
          ctx.reg_ctx->GetRegisterInfoAtIndex(i + reg_offset);
      RegisterValue reg_val;
      if (ctx.reg_ctx->ReadRegister(reg, reg_val))
        arg.value = reg_val.GetAsUInt64(0, &success);
    }
    // arguments passed on the stack
    else {
      // get the argument type size
      const size_t arg_size = sizeof(uint64_t);
      // clear all 64bits
      arg.value = 0;
      // read this argument from memory
      size_t bytes_read =
          ctx.process->ReadMemory(sp, &arg.value, arg_size, err);
      success = (err.Success() && bytes_read == arg_size);
      // advance the stack pointer
      sp += arg_size;
    }
    // fail if we couldn't read this argument
    if (!success) {
      if (log)
        log->Printf("%s - error reading argument: %" PRIu64 ", reason: %s",
                    __FUNCTION__, uint64_t(i), err.AsCString("n/a"));
      return false;
    }
  }
  return true;
}

bool GetArgs(ExecutionContext &exe_ctx, ArgItem *arg_list, size_t num_args) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  // verify that we have a target
  if (!exe_ctx.GetTargetPtr()) {
    if (log)
      log->Printf("%s - invalid target", __FUNCTION__);
    return false;
  }

  GetArgsCtx ctx = {exe_ctx.GetRegisterContext(), exe_ctx.GetProcessPtr()};
  assert(ctx.reg_ctx && ctx.process);

  // dispatch based on architecture
  switch (exe_ctx.GetTargetPtr()->GetArchitecture().GetMachine()) {
  case llvm::Triple::ArchType::x86:
    return GetArgsX86(ctx, arg_list, num_args);

  case llvm::Triple::ArchType::x86_64:
    return GetArgsX86_64(ctx, arg_list, num_args);

  case llvm::Triple::ArchType::arm:
    return GetArgsArm(ctx, arg_list, num_args);

  case llvm::Triple::ArchType::aarch64:
    return GetArgsAarch64(ctx, arg_list, num_args);

  case llvm::Triple::ArchType::mipsel:
    return GetArgsMipsel(ctx, arg_list, num_args);

  case llvm::Triple::ArchType::mips64el:
    return GetArgsMips64el(ctx, arg_list, num_args);

  default:
    // unsupported architecture
    if (log) {
      log->Printf(
          "%s - architecture not supported: '%s'", __FUNCTION__,
          exe_ctx.GetTargetRef().GetArchitecture().GetArchitectureName());
    }
    return false;
  }
}

bool IsRenderScriptScriptModule(ModuleSP module) {
  if (!module)
    return false;
  return module->FindFirstSymbolWithNameAndType(ConstString(".rs.info"),
                                                eSymbolTypeData) != nullptr;
}

bool ParseCoordinate(llvm::StringRef coord_s, RSCoordinate &coord) {
  // takes an argument of the form 'num[,num][,num]'. Where 'coord_s' is a
  // comma separated 1,2 or 3-dimensional coordinate with the whitespace
  // trimmed. Missing coordinates are defaulted to zero. If parsing of any
  // elements fails the contents of &coord are undefined and `false` is
  // returned, `true` otherwise

  RegularExpression regex;
  RegularExpression::Match regex_match(3);

  bool matched = false;
  if (regex.Compile(llvm::StringRef("^([0-9]+),([0-9]+),([0-9]+)$")) &&
      regex.Execute(coord_s, &regex_match))
    matched = true;
  else if (regex.Compile(llvm::StringRef("^([0-9]+),([0-9]+)$")) &&
           regex.Execute(coord_s, &regex_match))
    matched = true;
  else if (regex.Compile(llvm::StringRef("^([0-9]+)$")) &&
           regex.Execute(coord_s, &regex_match))
    matched = true;

  if (!matched)
    return false;

  auto get_index = [&](int idx, uint32_t &i) -> bool {
    std::string group;
    errno = 0;
    if (regex_match.GetMatchAtIndex(coord_s.str().c_str(), idx + 1, group))
      return !llvm::StringRef(group).getAsInteger<uint32_t>(10, i);
    return true;
  };

  return get_index(0, coord.x) && get_index(1, coord.y) &&
         get_index(2, coord.z);
}

bool SkipPrologue(lldb::ModuleSP &module, Address &addr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));
  SymbolContext sc;
  uint32_t resolved_flags =
      module->ResolveSymbolContextForAddress(addr, eSymbolContextFunction, sc);
  if (resolved_flags & eSymbolContextFunction) {
    if (sc.function) {
      const uint32_t offset = sc.function->GetPrologueByteSize();
      ConstString name = sc.GetFunctionName();
      if (offset)
        addr.Slide(offset);
      if (log)
        log->Printf("%s: Prologue offset for %s is %" PRIu32, __FUNCTION__,
                    name.AsCString(), offset);
    }
    return true;
  } else
    return false;
}
} // anonymous namespace

// The ScriptDetails class collects data associated with a single script
// instance.
struct RenderScriptRuntime::ScriptDetails {
  ~ScriptDetails() = default;

  enum ScriptType { eScript, eScriptC };

  // The derived type of the script.
  empirical_type<ScriptType> type;
  // The name of the original source file.
  empirical_type<std::string> res_name;
  // Path to script .so file on the device.
  empirical_type<std::string> shared_lib;
  // Directory where kernel objects are cached on device.
  empirical_type<std::string> cache_dir;
  // Pointer to the context which owns this script.
  empirical_type<lldb::addr_t> context;
  // Pointer to the script object itself.
  empirical_type<lldb::addr_t> script;
};

// This Element class represents the Element object in RS, defining the type
// associated with an Allocation.
struct RenderScriptRuntime::Element {
  // Taken from rsDefines.h
  enum DataKind {
    RS_KIND_USER,
    RS_KIND_PIXEL_L = 7,
    RS_KIND_PIXEL_A,
    RS_KIND_PIXEL_LA,
    RS_KIND_PIXEL_RGB,
    RS_KIND_PIXEL_RGBA,
    RS_KIND_PIXEL_DEPTH,
    RS_KIND_PIXEL_YUV,
    RS_KIND_INVALID = 100
  };

  // Taken from rsDefines.h
  enum DataType {
    RS_TYPE_NONE = 0,
    RS_TYPE_FLOAT_16,
    RS_TYPE_FLOAT_32,
    RS_TYPE_FLOAT_64,
    RS_TYPE_SIGNED_8,
    RS_TYPE_SIGNED_16,
    RS_TYPE_SIGNED_32,
    RS_TYPE_SIGNED_64,
    RS_TYPE_UNSIGNED_8,
    RS_TYPE_UNSIGNED_16,
    RS_TYPE_UNSIGNED_32,
    RS_TYPE_UNSIGNED_64,
    RS_TYPE_BOOLEAN,

    RS_TYPE_UNSIGNED_5_6_5,
    RS_TYPE_UNSIGNED_5_5_5_1,
    RS_TYPE_UNSIGNED_4_4_4_4,

    RS_TYPE_MATRIX_4X4,
    RS_TYPE_MATRIX_3X3,
    RS_TYPE_MATRIX_2X2,

    RS_TYPE_ELEMENT = 1000,
    RS_TYPE_TYPE,
    RS_TYPE_ALLOCATION,
    RS_TYPE_SAMPLER,
    RS_TYPE_SCRIPT,
    RS_TYPE_MESH,
    RS_TYPE_PROGRAM_FRAGMENT,
    RS_TYPE_PROGRAM_VERTEX,
    RS_TYPE_PROGRAM_RASTER,
    RS_TYPE_PROGRAM_STORE,
    RS_TYPE_FONT,

    RS_TYPE_INVALID = 10000
  };

  std::vector<Element> children; // Child Element fields for structs
  empirical_type<lldb::addr_t>
      element_ptr; // Pointer to the RS Element of the Type
  empirical_type<DataType>
      type; // Type of each data pointer stored by the allocation
  empirical_type<DataKind>
      type_kind; // Defines pixel type if Allocation is created from an image
  empirical_type<uint32_t>
      type_vec_size; // Vector size of each data point, e.g '4' for uchar4
  empirical_type<uint32_t> field_count; // Number of Subelements
  empirical_type<uint32_t> datum_size;  // Size of a single Element with padding
  empirical_type<uint32_t> padding;     // Number of padding bytes
  empirical_type<uint32_t>
      array_size;        // Number of items in array, only needed for structs
  ConstString type_name; // Name of type, only needed for structs

  static const ConstString &
  GetFallbackStructName(); // Print this as the type name of a struct Element
                           // If we can't resolve the actual struct name

  bool ShouldRefresh() const {
    const bool valid_ptr = element_ptr.isValid() && *element_ptr.get() != 0x0;
    const bool valid_type =
        type.isValid() && type_vec_size.isValid() && type_kind.isValid();
    return !valid_ptr || !valid_type || !datum_size.isValid();
  }
};

// This AllocationDetails class collects data associated with a single
// allocation instance.
struct RenderScriptRuntime::AllocationDetails {
  struct Dimension {
    uint32_t dim_1;
    uint32_t dim_2;
    uint32_t dim_3;
    uint32_t cube_map;

    Dimension() {
      dim_1 = 0;
      dim_2 = 0;
      dim_3 = 0;
      cube_map = 0;
    }
  };

  // The FileHeader struct specifies the header we use for writing allocations
  // to a binary file. Our format begins with the ASCII characters "RSAD",
  // identifying the file as an allocation dump. Member variables dims and
  // hdr_size are then written consecutively, immediately followed by an
  // instance of the ElementHeader struct. Because Elements can contain
  // subelements, there may be more than one instance of the ElementHeader
  // struct. With this first instance being the root element, and the other
  // instances being the root's descendants. To identify which instances are an
  // ElementHeader's children, each struct is immediately followed by a
  // sequence of consecutive offsets to the start of its child structs. These
  // offsets are
  // 4 bytes in size, and the 0 offset signifies no more children.
  struct FileHeader {
    uint8_t ident[4];  // ASCII 'RSAD' identifying the file
    uint32_t dims[3];  // Dimensions
    uint16_t hdr_size; // Header size in bytes, including all element headers
  };

  struct ElementHeader {
    uint16_t type;         // DataType enum
    uint32_t kind;         // DataKind enum
    uint32_t element_size; // Size of a single element, including padding
    uint16_t vector_size;  // Vector width
    uint32_t array_size;   // Number of elements in array
  };

  // Monotonically increasing from 1
  static uint32_t ID;

  // Maps Allocation DataType enum and vector size to printable strings using
  // mapping from RenderScript numerical types summary documentation
  static const char *RsDataTypeToString[][4];

  // Maps Allocation DataKind enum to printable strings
  static const char *RsDataKindToString[];

  // Maps allocation types to format sizes for printing.
  static const uint32_t RSTypeToFormat[][3];

  // Give each allocation an ID as a way
  // for commands to reference it.
  const uint32_t id;

  // Allocation Element type
  RenderScriptRuntime::Element element;
  // Dimensions of the Allocation
  empirical_type<Dimension> dimension;
  // Pointer to address of the RS Allocation
  empirical_type<lldb::addr_t> address;
  // Pointer to the data held by the Allocation
  empirical_type<lldb::addr_t> data_ptr;
  // Pointer to the RS Type of the Allocation
  empirical_type<lldb::addr_t> type_ptr;
  // Pointer to the RS Context of the Allocation
  empirical_type<lldb::addr_t> context;
  // Size of the allocation
  empirical_type<uint32_t> size;
  // Stride between rows of the allocation
  empirical_type<uint32_t> stride;

  // Give each allocation an id, so we can reference it in user commands.
  AllocationDetails() : id(ID++) {}

  bool ShouldRefresh() const {
    bool valid_ptrs = data_ptr.isValid() && *data_ptr.get() != 0x0;
    valid_ptrs = valid_ptrs && type_ptr.isValid() && *type_ptr.get() != 0x0;
    return !valid_ptrs || !dimension.isValid() || !size.isValid() ||
           element.ShouldRefresh();
  }
};

const ConstString &RenderScriptRuntime::Element::GetFallbackStructName() {
  static const ConstString FallbackStructName("struct");
  return FallbackStructName;
}

uint32_t RenderScriptRuntime::AllocationDetails::ID = 1;

const char *RenderScriptRuntime::AllocationDetails::RsDataKindToString[] = {
    "User",       "Undefined",   "Undefined", "Undefined",
    "Undefined",  "Undefined",   "Undefined", // Enum jumps from 0 to 7
    "L Pixel",    "A Pixel",     "LA Pixel",  "RGB Pixel",
    "RGBA Pixel", "Pixel Depth", "YUV Pixel"};

const char *RenderScriptRuntime::AllocationDetails::RsDataTypeToString[][4] = {
    {"None", "None", "None", "None"},
    {"half", "half2", "half3", "half4"},
    {"float", "float2", "float3", "float4"},
    {"double", "double2", "double3", "double4"},
    {"char", "char2", "char3", "char4"},
    {"short", "short2", "short3", "short4"},
    {"int", "int2", "int3", "int4"},
    {"long", "long2", "long3", "long4"},
    {"uchar", "uchar2", "uchar3", "uchar4"},
    {"ushort", "ushort2", "ushort3", "ushort4"},
    {"uint", "uint2", "uint3", "uint4"},
    {"ulong", "ulong2", "ulong3", "ulong4"},
    {"bool", "bool2", "bool3", "bool4"},
    {"packed_565", "packed_565", "packed_565", "packed_565"},
    {"packed_5551", "packed_5551", "packed_5551", "packed_5551"},
    {"packed_4444", "packed_4444", "packed_4444", "packed_4444"},
    {"rs_matrix4x4", "rs_matrix4x4", "rs_matrix4x4", "rs_matrix4x4"},
    {"rs_matrix3x3", "rs_matrix3x3", "rs_matrix3x3", "rs_matrix3x3"},
    {"rs_matrix2x2", "rs_matrix2x2", "rs_matrix2x2", "rs_matrix2x2"},

    // Handlers
    {"RS Element", "RS Element", "RS Element", "RS Element"},
    {"RS Type", "RS Type", "RS Type", "RS Type"},
    {"RS Allocation", "RS Allocation", "RS Allocation", "RS Allocation"},
    {"RS Sampler", "RS Sampler", "RS Sampler", "RS Sampler"},
    {"RS Script", "RS Script", "RS Script", "RS Script"},

    // Deprecated
    {"RS Mesh", "RS Mesh", "RS Mesh", "RS Mesh"},
    {"RS Program Fragment", "RS Program Fragment", "RS Program Fragment",
     "RS Program Fragment"},
    {"RS Program Vertex", "RS Program Vertex", "RS Program Vertex",
     "RS Program Vertex"},
    {"RS Program Raster", "RS Program Raster", "RS Program Raster",
     "RS Program Raster"},
    {"RS Program Store", "RS Program Store", "RS Program Store",
     "RS Program Store"},
    {"RS Font", "RS Font", "RS Font", "RS Font"}};

// Used as an index into the RSTypeToFormat array elements
enum TypeToFormatIndex { eFormatSingle = 0, eFormatVector, eElementSize };

// { format enum of single element, format enum of element vector, size of
// element}
const uint32_t RenderScriptRuntime::AllocationDetails::RSTypeToFormat[][3] = {
    // RS_TYPE_NONE
    {eFormatHex, eFormatHex, 1},
    // RS_TYPE_FLOAT_16
    {eFormatFloat, eFormatVectorOfFloat16, 2},
    // RS_TYPE_FLOAT_32
    {eFormatFloat, eFormatVectorOfFloat32, sizeof(float)},
    // RS_TYPE_FLOAT_64
    {eFormatFloat, eFormatVectorOfFloat64, sizeof(double)},
    // RS_TYPE_SIGNED_8
    {eFormatDecimal, eFormatVectorOfSInt8, sizeof(int8_t)},
    // RS_TYPE_SIGNED_16
    {eFormatDecimal, eFormatVectorOfSInt16, sizeof(int16_t)},
    // RS_TYPE_SIGNED_32
    {eFormatDecimal, eFormatVectorOfSInt32, sizeof(int32_t)},
    // RS_TYPE_SIGNED_64
    {eFormatDecimal, eFormatVectorOfSInt64, sizeof(int64_t)},
    // RS_TYPE_UNSIGNED_8
    {eFormatDecimal, eFormatVectorOfUInt8, sizeof(uint8_t)},
    // RS_TYPE_UNSIGNED_16
    {eFormatDecimal, eFormatVectorOfUInt16, sizeof(uint16_t)},
    // RS_TYPE_UNSIGNED_32
    {eFormatDecimal, eFormatVectorOfUInt32, sizeof(uint32_t)},
    // RS_TYPE_UNSIGNED_64
    {eFormatDecimal, eFormatVectorOfUInt64, sizeof(uint64_t)},
    // RS_TYPE_BOOL
    {eFormatBoolean, eFormatBoolean, 1},
    // RS_TYPE_UNSIGNED_5_6_5
    {eFormatHex, eFormatHex, sizeof(uint16_t)},
    // RS_TYPE_UNSIGNED_5_5_5_1
    {eFormatHex, eFormatHex, sizeof(uint16_t)},
    // RS_TYPE_UNSIGNED_4_4_4_4
    {eFormatHex, eFormatHex, sizeof(uint16_t)},
    // RS_TYPE_MATRIX_4X4
    {eFormatVectorOfFloat32, eFormatVectorOfFloat32, sizeof(float) * 16},
    // RS_TYPE_MATRIX_3X3
    {eFormatVectorOfFloat32, eFormatVectorOfFloat32, sizeof(float) * 9},
    // RS_TYPE_MATRIX_2X2
    {eFormatVectorOfFloat32, eFormatVectorOfFloat32, sizeof(float) * 4}};

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------
LanguageRuntime *
RenderScriptRuntime::CreateInstance(Process *process,
                                    lldb::LanguageType language) {

  if (language == eLanguageTypeExtRenderScript)
    return new RenderScriptRuntime(process);
  else
    return nullptr;
}

// Callback with a module to search for matching symbols. We first check that
// the module contains RS kernels. Then look for a symbol which matches our
// kernel name. The breakpoint address is finally set using the address of this
// symbol.
Searcher::CallbackReturn
RSBreakpointResolver::SearchCallback(SearchFilter &filter,
                                     SymbolContext &context, Address *, bool) {
  ModuleSP module = context.module_sp;

  if (!module || !IsRenderScriptScriptModule(module))
    return Searcher::eCallbackReturnContinue;

  // Attempt to set a breakpoint on the kernel name symbol within the module
  // library. If it's not found, it's likely debug info is unavailable - try to
  // set a breakpoint on <name>.expand.
  const Symbol *kernel_sym =
      module->FindFirstSymbolWithNameAndType(m_kernel_name, eSymbolTypeCode);
  if (!kernel_sym) {
    std::string kernel_name_expanded(m_kernel_name.AsCString());
    kernel_name_expanded.append(".expand");
    kernel_sym = module->FindFirstSymbolWithNameAndType(
        ConstString(kernel_name_expanded.c_str()), eSymbolTypeCode);
  }

  if (kernel_sym) {
    Address bp_addr = kernel_sym->GetAddress();
    if (filter.AddressPasses(bp_addr))
      m_breakpoint->AddLocation(bp_addr);
  }

  return Searcher::eCallbackReturnContinue;
}

Searcher::CallbackReturn
RSReduceBreakpointResolver::SearchCallback(lldb_private::SearchFilter &filter,
                                           lldb_private::SymbolContext &context,
                                           Address *, bool) {
  // We need to have access to the list of reductions currently parsed, as
  // reduce names don't actually exist as symbols in a module. They are only
  // identifiable by parsing the .rs.info packet, or finding the expand symbol.
  // We therefore need access to the list of parsed rs modules to properly
  // resolve reduction names.
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  ModuleSP module = context.module_sp;

  if (!module || !IsRenderScriptScriptModule(module))
    return Searcher::eCallbackReturnContinue;

  if (!m_rsmodules)
    return Searcher::eCallbackReturnContinue;

  for (const auto &module_desc : *m_rsmodules) {
    if (module_desc->m_module != module)
      continue;

    for (const auto &reduction : module_desc->m_reductions) {
      if (reduction.m_reduce_name != m_reduce_name)
        continue;

      std::array<std::pair<ConstString, int>, 5> funcs{
          {{reduction.m_init_name, eKernelTypeInit},
           {reduction.m_accum_name, eKernelTypeAccum},
           {reduction.m_comb_name, eKernelTypeComb},
           {reduction.m_outc_name, eKernelTypeOutC},
           {reduction.m_halter_name, eKernelTypeHalter}}};

      for (const auto &kernel : funcs) {
        // Skip constituent functions that don't match our spec
        if (!(m_kernel_types & kernel.second))
          continue;

        const auto kernel_name = kernel.first;
        const auto symbol = module->FindFirstSymbolWithNameAndType(
            kernel_name, eSymbolTypeCode);
        if (!symbol)
          continue;

        auto address = symbol->GetAddress();
        if (filter.AddressPasses(address)) {
          bool new_bp;
          if (!SkipPrologue(module, address)) {
            if (log)
              log->Printf("%s: Error trying to skip prologue", __FUNCTION__);
          }
          m_breakpoint->AddLocation(address, &new_bp);
          if (log)
            log->Printf("%s: %s reduction breakpoint on %s in %s", __FUNCTION__,
                        new_bp ? "new" : "existing", kernel_name.GetCString(),
                        address.GetModule()->GetFileSpec().GetCString());
        }
      }
    }
  }
  return eCallbackReturnContinue;
}

Searcher::CallbackReturn RSScriptGroupBreakpointResolver::SearchCallback(
    SearchFilter &filter, SymbolContext &context, Address *addr,
    bool containing) {

  if (!m_breakpoint)
    return eCallbackReturnContinue;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  ModuleSP &module = context.module_sp;

  if (!module || !IsRenderScriptScriptModule(module))
    return Searcher::eCallbackReturnContinue;

  std::vector<std::string> names;
  m_breakpoint->GetNames(names);
  if (names.empty())
    return eCallbackReturnContinue;

  for (auto &name : names) {
    const RSScriptGroupDescriptorSP sg = FindScriptGroup(ConstString(name));
    if (!sg) {
      if (log)
        log->Printf("%s: could not find script group for %s", __FUNCTION__,
                    name.c_str());
      continue;
    }

    if (log)
      log->Printf("%s: Found ScriptGroup for %s", __FUNCTION__, name.c_str());

    for (const RSScriptGroupDescriptor::Kernel &k : sg->m_kernels) {
      if (log) {
        log->Printf("%s: Adding breakpoint for %s", __FUNCTION__,
                    k.m_name.AsCString());
        log->Printf("%s: Kernel address 0x%" PRIx64, __FUNCTION__, k.m_addr);
      }

      const lldb_private::Symbol *sym =
          module->FindFirstSymbolWithNameAndType(k.m_name, eSymbolTypeCode);
      if (!sym) {
        if (log)
          log->Printf("%s: Unable to find symbol for %s", __FUNCTION__,
                      k.m_name.AsCString());
        continue;
      }

      if (log) {
        log->Printf("%s: Found symbol name is %s", __FUNCTION__,
                    sym->GetName().AsCString());
      }

      auto address = sym->GetAddress();
      if (!SkipPrologue(module, address)) {
        if (log)
          log->Printf("%s: Error trying to skip prologue", __FUNCTION__);
      }

      bool new_bp;
      m_breakpoint->AddLocation(address, &new_bp);

      if (log)
        log->Printf("%s: Placed %sbreakpoint on %s", __FUNCTION__,
                    new_bp ? "new " : "", k.m_name.AsCString());

      // exit after placing the first breakpoint if we do not intend to stop on
      // all kernels making up this script group
      if (!m_stop_on_all)
        break;
    }
  }

  return eCallbackReturnContinue;
}

void RenderScriptRuntime::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "RenderScript language support", CreateInstance,
                                GetCommandObject);
}

void RenderScriptRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString RenderScriptRuntime::GetPluginNameStatic() {
  static ConstString plugin_name("renderscript");
  return plugin_name;
}

RenderScriptRuntime::ModuleKind
RenderScriptRuntime::GetModuleKind(const lldb::ModuleSP &module_sp) {
  if (module_sp) {
    if (IsRenderScriptScriptModule(module_sp))
      return eModuleKindKernelObj;

    // Is this the main RS runtime library
    const ConstString rs_lib("libRS.so");
    if (module_sp->GetFileSpec().GetFilename() == rs_lib) {
      return eModuleKindLibRS;
    }

    const ConstString rs_driverlib("libRSDriver.so");
    if (module_sp->GetFileSpec().GetFilename() == rs_driverlib) {
      return eModuleKindDriver;
    }

    const ConstString rs_cpureflib("libRSCpuRef.so");
    if (module_sp->GetFileSpec().GetFilename() == rs_cpureflib) {
      return eModuleKindImpl;
    }
  }
  return eModuleKindIgnored;
}

bool RenderScriptRuntime::IsRenderScriptModule(
    const lldb::ModuleSP &module_sp) {
  return GetModuleKind(module_sp) != eModuleKindIgnored;
}

void RenderScriptRuntime::ModulesDidLoad(const ModuleList &module_list) {
  std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());

  size_t num_modules = module_list.GetSize();
  for (size_t i = 0; i < num_modules; i++) {
    auto mod = module_list.GetModuleAtIndex(i);
    if (IsRenderScriptModule(mod)) {
      LoadModule(mod);
    }
  }
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString RenderScriptRuntime::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t RenderScriptRuntime::GetPluginVersion() { return 1; }

bool RenderScriptRuntime::IsVTableName(const char *name) { return false; }

bool RenderScriptRuntime::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &address,
    Value::ValueType &value_type) {
  return false;
}

TypeAndOrName
RenderScriptRuntime::FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                      ValueObject &static_value) {
  return type_and_or_name;
}

bool RenderScriptRuntime::CouldHaveDynamicValue(ValueObject &in_value) {
  return false;
}

lldb::BreakpointResolverSP
RenderScriptRuntime::CreateExceptionResolver(Breakpoint *bp, bool catch_bp,
                                             bool throw_bp) {
  BreakpointResolverSP resolver_sp;
  return resolver_sp;
}

const RenderScriptRuntime::HookDefn RenderScriptRuntime::s_runtimeHookDefns[] =
    {
        // rsdScript
        {"rsdScriptInit", "_Z13rsdScriptInitPKN7android12renderscript7ContextEP"
                          "NS0_7ScriptCEPKcS7_PKhjj",
         "_Z13rsdScriptInitPKN7android12renderscript7ContextEPNS0_"
         "7ScriptCEPKcS7_PKhmj",
         0, RenderScriptRuntime::eModuleKindDriver,
         &lldb_private::RenderScriptRuntime::CaptureScriptInit},
        {"rsdScriptInvokeForEachMulti",
         "_Z27rsdScriptInvokeForEachMultiPKN7android12renderscript7ContextEPNS0"
         "_6ScriptEjPPKNS0_10AllocationEjPS6_PKvjPK12RsScriptCall",
         "_Z27rsdScriptInvokeForEachMultiPKN7android12renderscript7ContextEPNS0"
         "_6ScriptEjPPKNS0_10AllocationEmPS6_PKvmPK12RsScriptCall",
         0, RenderScriptRuntime::eModuleKindDriver,
         &lldb_private::RenderScriptRuntime::CaptureScriptInvokeForEachMulti},
        {"rsdScriptSetGlobalVar", "_Z21rsdScriptSetGlobalVarPKN7android12render"
                                  "script7ContextEPKNS0_6ScriptEjPvj",
         "_Z21rsdScriptSetGlobalVarPKN7android12renderscript7ContextEPKNS0_"
         "6ScriptEjPvm",
         0, RenderScriptRuntime::eModuleKindDriver,
         &lldb_private::RenderScriptRuntime::CaptureSetGlobalVar},

        // rsdAllocation
        {"rsdAllocationInit", "_Z17rsdAllocationInitPKN7android12renderscript7C"
                              "ontextEPNS0_10AllocationEb",
         "_Z17rsdAllocationInitPKN7android12renderscript7ContextEPNS0_"
         "10AllocationEb",
         0, RenderScriptRuntime::eModuleKindDriver,
         &lldb_private::RenderScriptRuntime::CaptureAllocationInit},
        {"rsdAllocationRead2D",
         "_Z19rsdAllocationRead2DPKN7android12renderscript7ContextEPKNS0_"
         "10AllocationEjjj23RsAllocationCubemapFacejjPvjj",
         "_Z19rsdAllocationRead2DPKN7android12renderscript7ContextEPKNS0_"
         "10AllocationEjjj23RsAllocationCubemapFacejjPvmm",
         0, RenderScriptRuntime::eModuleKindDriver, nullptr},
        {"rsdAllocationDestroy", "_Z20rsdAllocationDestroyPKN7android12rendersc"
                                 "ript7ContextEPNS0_10AllocationE",
         "_Z20rsdAllocationDestroyPKN7android12renderscript7ContextEPNS0_"
         "10AllocationE",
         0, RenderScriptRuntime::eModuleKindDriver,
         &lldb_private::RenderScriptRuntime::CaptureAllocationDestroy},

        // renderscript script groups
        {"rsdDebugHintScriptGroup2", "_ZN7android12renderscript21debugHintScrip"
                                     "tGroup2EPKcjPKPFvPK24RsExpandKernelDriver"
                                     "InfojjjEj",
         "_ZN7android12renderscript21debugHintScriptGroup2EPKcjPKPFvPK24RsExpan"
         "dKernelDriverInfojjjEj",
         0, RenderScriptRuntime::eModuleKindImpl,
         &lldb_private::RenderScriptRuntime::CaptureDebugHintScriptGroup2}};

const size_t RenderScriptRuntime::s_runtimeHookCount =
    sizeof(s_runtimeHookDefns) / sizeof(s_runtimeHookDefns[0]);

bool RenderScriptRuntime::HookCallback(void *baton,
                                       StoppointCallbackContext *ctx,
                                       lldb::user_id_t break_id,
                                       lldb::user_id_t break_loc_id) {
  RuntimeHook *hook = (RuntimeHook *)baton;
  ExecutionContext exe_ctx(ctx->exe_ctx_ref);

  RenderScriptRuntime *lang_rt =
      (RenderScriptRuntime *)exe_ctx.GetProcessPtr()->GetLanguageRuntime(
          eLanguageTypeExtRenderScript);

  lang_rt->HookCallback(hook, exe_ctx);

  return false;
}

void RenderScriptRuntime::HookCallback(RuntimeHook *hook,
                                       ExecutionContext &exe_ctx) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (log)
    log->Printf("%s - '%s'", __FUNCTION__, hook->defn->name);

  if (hook->defn->grabber) {
    (this->*(hook->defn->grabber))(hook, exe_ctx);
  }
}

void RenderScriptRuntime::CaptureDebugHintScriptGroup2(
    RuntimeHook *hook_info, ExecutionContext &context) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  enum {
    eGroupName = 0,
    eGroupNameSize,
    eKernel,
    eKernelCount,
  };

  std::array<ArgItem, 4> args{{
      {ArgItem::ePointer, 0}, // const char         *groupName
      {ArgItem::eInt32, 0},   // const uint32_t      groupNameSize
      {ArgItem::ePointer, 0}, // const ExpandFuncTy *kernel
      {ArgItem::eInt32, 0},   // const uint32_t      kernelCount
  }};

  if (!GetArgs(context, args.data(), args.size())) {
    if (log)
      log->Printf("%s - Error while reading the function parameters",
                  __FUNCTION__);
    return;
  } else if (log) {
    log->Printf("%s - groupName    : 0x%" PRIx64, __FUNCTION__,
                addr_t(args[eGroupName]));
    log->Printf("%s - groupNameSize: %" PRIu64, __FUNCTION__,
                uint64_t(args[eGroupNameSize]));
    log->Printf("%s - kernel       : 0x%" PRIx64, __FUNCTION__,
                addr_t(args[eKernel]));
    log->Printf("%s - kernelCount  : %" PRIu64, __FUNCTION__,
                uint64_t(args[eKernelCount]));
  }

  // parse script group name
  ConstString group_name;
  {
    Status err;
    const uint64_t len = uint64_t(args[eGroupNameSize]);
    std::unique_ptr<char[]> buffer(new char[uint32_t(len + 1)]);
    m_process->ReadMemory(addr_t(args[eGroupName]), buffer.get(), len, err);
    buffer.get()[len] = '\0';
    if (!err.Success()) {
      if (log)
        log->Printf("Error reading scriptgroup name from target");
      return;
    } else {
      if (log)
        log->Printf("Extracted scriptgroup name %s", buffer.get());
    }
    // write back the script group name
    group_name.SetCString(buffer.get());
  }

  // create or access existing script group
  RSScriptGroupDescriptorSP group;
  {
    // search for existing script group
    for (auto sg : m_scriptGroups) {
      if (sg->m_name == group_name) {
        group = sg;
        break;
      }
    }
    if (!group) {
      group.reset(new RSScriptGroupDescriptor);
      group->m_name = group_name;
      m_scriptGroups.push_back(group);
    } else {
      // already have this script group
      if (log)
        log->Printf("Attempt to add duplicate script group %s",
                    group_name.AsCString());
      return;
    }
  }
  assert(group);

  const uint32_t target_ptr_size = m_process->GetAddressByteSize();
  std::vector<addr_t> kernels;
  // parse kernel addresses in script group
  for (uint64_t i = 0; i < uint64_t(args[eKernelCount]); ++i) {
    RSScriptGroupDescriptor::Kernel kernel;
    // extract script group kernel addresses from the target
    const addr_t ptr_addr = addr_t(args[eKernel]) + i * target_ptr_size;
    uint64_t kernel_addr = 0;
    Status err;
    size_t read =
        m_process->ReadMemory(ptr_addr, &kernel_addr, target_ptr_size, err);
    if (!err.Success() || read != target_ptr_size) {
      if (log)
        log->Printf("Error parsing kernel address %" PRIu64 " in script group",
                    i);
      return;
    }
    if (log)
      log->Printf("Extracted scriptgroup kernel address - 0x%" PRIx64,
                  kernel_addr);
    kernel.m_addr = kernel_addr;

    // try to resolve the associated kernel name
    if (!ResolveKernelName(kernel.m_addr, kernel.m_name)) {
      if (log)
        log->Printf("Parsed scriptgroup kernel %" PRIu64 " - 0x%" PRIx64, i,
                    kernel_addr);
      return;
    }

    // try to find the non '.expand' function
    {
      const llvm::StringRef expand(".expand");
      const llvm::StringRef name_ref = kernel.m_name.GetStringRef();
      if (name_ref.endswith(expand)) {
        const ConstString base_kernel(name_ref.drop_back(expand.size()));
        // verify this function is a valid kernel
        if (IsKnownKernel(base_kernel)) {
          kernel.m_name = base_kernel;
          if (log)
            log->Printf("%s - found non expand version '%s'", __FUNCTION__,
                        base_kernel.GetCString());
        }
      }
    }
    // add to a list of script group kernels we know about
    group->m_kernels.push_back(kernel);
  }

  // Resolve any pending scriptgroup breakpoints
  {
    Target &target = m_process->GetTarget();
    const BreakpointList &list = target.GetBreakpointList();
    const size_t num_breakpoints = list.GetSize();
    if (log)
      log->Printf("Resolving %zu breakpoints", num_breakpoints);
    for (size_t i = 0; i < num_breakpoints; ++i) {
      const BreakpointSP bp = list.GetBreakpointAtIndex(i);
      if (bp) {
        if (bp->MatchesName(group_name.AsCString())) {
          if (log)
            log->Printf("Found breakpoint with name %s",
                        group_name.AsCString());
          bp->ResolveBreakpoint();
        }
      }
    }
  }
}

void RenderScriptRuntime::CaptureScriptInvokeForEachMulti(
    RuntimeHook *hook, ExecutionContext &exe_ctx) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  enum {
    eRsContext = 0,
    eRsScript,
    eRsSlot,
    eRsAIns,
    eRsInLen,
    eRsAOut,
    eRsUsr,
    eRsUsrLen,
    eRsSc,
  };

  std::array<ArgItem, 9> args{{
      ArgItem{ArgItem::ePointer, 0}, // const Context       *rsc
      ArgItem{ArgItem::ePointer, 0}, // Script              *s
      ArgItem{ArgItem::eInt32, 0},   // uint32_t             slot
      ArgItem{ArgItem::ePointer, 0}, // const Allocation   **aIns
      ArgItem{ArgItem::eInt32, 0},   // size_t               inLen
      ArgItem{ArgItem::ePointer, 0}, // Allocation          *aout
      ArgItem{ArgItem::ePointer, 0}, // const void          *usr
      ArgItem{ArgItem::eInt32, 0},   // size_t               usrLen
      ArgItem{ArgItem::ePointer, 0}, // const RsScriptCall  *sc
  }};

  bool success = GetArgs(exe_ctx, &args[0], args.size());
  if (!success) {
    if (log)
      log->Printf("%s - Error while reading the function parameters",
                  __FUNCTION__);
    return;
  }

  const uint32_t target_ptr_size = m_process->GetAddressByteSize();
  Status err;
  std::vector<uint64_t> allocs;

  // traverse allocation list
  for (uint64_t i = 0; i < uint64_t(args[eRsInLen]); ++i) {
    // calculate offest to allocation pointer
    const addr_t addr = addr_t(args[eRsAIns]) + i * target_ptr_size;

    // Note: due to little endian layout, reading 32bits or 64bits into res
    // will give the correct results.
    uint64_t result = 0;
    size_t read = m_process->ReadMemory(addr, &result, target_ptr_size, err);
    if (read != target_ptr_size || !err.Success()) {
      if (log)
        log->Printf(
            "%s - Error while reading allocation list argument %" PRIu64,
            __FUNCTION__, i);
    } else {
      allocs.push_back(result);
    }
  }

  // if there is an output allocation track it
  if (uint64_t alloc_out = uint64_t(args[eRsAOut])) {
    allocs.push_back(alloc_out);
  }

  // for all allocations we have found
  for (const uint64_t alloc_addr : allocs) {
    AllocationDetails *alloc = LookUpAllocation(alloc_addr);
    if (!alloc)
      alloc = CreateAllocation(alloc_addr);

    if (alloc) {
      // save the allocation address
      if (alloc->address.isValid()) {
        // check the allocation address we already have matches
        assert(*alloc->address.get() == alloc_addr);
      } else {
        alloc->address = alloc_addr;
      }

      // save the context
      if (log) {
        if (alloc->context.isValid() &&
            *alloc->context.get() != addr_t(args[eRsContext]))
          log->Printf("%s - Allocation used by multiple contexts",
                      __FUNCTION__);
      }
      alloc->context = addr_t(args[eRsContext]);
    }
  }

  // make sure we track this script object
  if (lldb_private::RenderScriptRuntime::ScriptDetails *script =
          LookUpScript(addr_t(args[eRsScript]), true)) {
    if (log) {
      if (script->context.isValid() &&
          *script->context.get() != addr_t(args[eRsContext]))
        log->Printf("%s - Script used by multiple contexts", __FUNCTION__);
    }
    script->context = addr_t(args[eRsContext]);
  }
}

void RenderScriptRuntime::CaptureSetGlobalVar(RuntimeHook *hook,
                                              ExecutionContext &context) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  enum {
    eRsContext,
    eRsScript,
    eRsId,
    eRsData,
    eRsLength,
  };

  std::array<ArgItem, 5> args{{
      ArgItem{ArgItem::ePointer, 0}, // eRsContext
      ArgItem{ArgItem::ePointer, 0}, // eRsScript
      ArgItem{ArgItem::eInt32, 0},   // eRsId
      ArgItem{ArgItem::ePointer, 0}, // eRsData
      ArgItem{ArgItem::eInt32, 0},   // eRsLength
  }};

  bool success = GetArgs(context, &args[0], args.size());
  if (!success) {
    if (log)
      log->Printf("%s - error reading the function parameters.", __FUNCTION__);
    return;
  }

  if (log) {
    log->Printf("%s - 0x%" PRIx64 ",0x%" PRIx64 " slot %" PRIu64 " = 0x%" PRIx64
                ":%" PRIu64 "bytes.",
                __FUNCTION__, uint64_t(args[eRsContext]),
                uint64_t(args[eRsScript]), uint64_t(args[eRsId]),
                uint64_t(args[eRsData]), uint64_t(args[eRsLength]));

    addr_t script_addr = addr_t(args[eRsScript]);
    if (m_scriptMappings.find(script_addr) != m_scriptMappings.end()) {
      auto rsm = m_scriptMappings[script_addr];
      if (uint64_t(args[eRsId]) < rsm->m_globals.size()) {
        auto rsg = rsm->m_globals[uint64_t(args[eRsId])];
        log->Printf("%s - Setting of '%s' within '%s' inferred", __FUNCTION__,
                    rsg.m_name.AsCString(),
                    rsm->m_module->GetFileSpec().GetFilename().AsCString());
      }
    }
  }
}

void RenderScriptRuntime::CaptureAllocationInit(RuntimeHook *hook,
                                                ExecutionContext &exe_ctx) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  enum { eRsContext, eRsAlloc, eRsForceZero };

  std::array<ArgItem, 3> args{{
      ArgItem{ArgItem::ePointer, 0}, // eRsContext
      ArgItem{ArgItem::ePointer, 0}, // eRsAlloc
      ArgItem{ArgItem::eBool, 0},    // eRsForceZero
  }};

  bool success = GetArgs(exe_ctx, &args[0], args.size());
  if (!success) {
    if (log)
      log->Printf("%s - error while reading the function parameters",
                  __FUNCTION__);
    return;
  }

  if (log)
    log->Printf("%s - 0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 " .",
                __FUNCTION__, uint64_t(args[eRsContext]),
                uint64_t(args[eRsAlloc]), uint64_t(args[eRsForceZero]));

  AllocationDetails *alloc = CreateAllocation(uint64_t(args[eRsAlloc]));
  if (alloc)
    alloc->context = uint64_t(args[eRsContext]);
}

void RenderScriptRuntime::CaptureAllocationDestroy(RuntimeHook *hook,
                                                   ExecutionContext &exe_ctx) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  enum {
    eRsContext,
    eRsAlloc,
  };

  std::array<ArgItem, 2> args{{
      ArgItem{ArgItem::ePointer, 0}, // eRsContext
      ArgItem{ArgItem::ePointer, 0}, // eRsAlloc
  }};

  bool success = GetArgs(exe_ctx, &args[0], args.size());
  if (!success) {
    if (log)
      log->Printf("%s - error while reading the function parameters.",
                  __FUNCTION__);
    return;
  }

  if (log)
    log->Printf("%s - 0x%" PRIx64 ", 0x%" PRIx64 ".", __FUNCTION__,
                uint64_t(args[eRsContext]), uint64_t(args[eRsAlloc]));

  for (auto iter = m_allocations.begin(); iter != m_allocations.end(); ++iter) {
    auto &allocation_ap = *iter; // get the unique pointer
    if (allocation_ap->address.isValid() &&
        *allocation_ap->address.get() == addr_t(args[eRsAlloc])) {
      m_allocations.erase(iter);
      if (log)
        log->Printf("%s - deleted allocation entry.", __FUNCTION__);
      return;
    }
  }

  if (log)
    log->Printf("%s - couldn't find destroyed allocation.", __FUNCTION__);
}

void RenderScriptRuntime::CaptureScriptInit(RuntimeHook *hook,
                                            ExecutionContext &exe_ctx) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  Status err;
  Process *process = exe_ctx.GetProcessPtr();

  enum { eRsContext, eRsScript, eRsResNamePtr, eRsCachedDirPtr };

  std::array<ArgItem, 4> args{
      {ArgItem{ArgItem::ePointer, 0}, ArgItem{ArgItem::ePointer, 0},
       ArgItem{ArgItem::ePointer, 0}, ArgItem{ArgItem::ePointer, 0}}};
  bool success = GetArgs(exe_ctx, &args[0], args.size());
  if (!success) {
    if (log)
      log->Printf("%s - error while reading the function parameters.",
                  __FUNCTION__);
    return;
  }

  std::string res_name;
  process->ReadCStringFromMemory(addr_t(args[eRsResNamePtr]), res_name, err);
  if (err.Fail()) {
    if (log)
      log->Printf("%s - error reading res_name: %s.", __FUNCTION__,
                  err.AsCString());
  }

  std::string cache_dir;
  process->ReadCStringFromMemory(addr_t(args[eRsCachedDirPtr]), cache_dir, err);
  if (err.Fail()) {
    if (log)
      log->Printf("%s - error reading cache_dir: %s.", __FUNCTION__,
                  err.AsCString());
  }

  if (log)
    log->Printf("%s - 0x%" PRIx64 ",0x%" PRIx64 " => '%s' at '%s' .",
                __FUNCTION__, uint64_t(args[eRsContext]),
                uint64_t(args[eRsScript]), res_name.c_str(), cache_dir.c_str());

  if (res_name.size() > 0) {
    StreamString strm;
    strm.Printf("librs.%s.so", res_name.c_str());

    ScriptDetails *script = LookUpScript(addr_t(args[eRsScript]), true);
    if (script) {
      script->type = ScriptDetails::eScriptC;
      script->cache_dir = cache_dir;
      script->res_name = res_name;
      script->shared_lib = strm.GetString();
      script->context = addr_t(args[eRsContext]);
    }

    if (log)
      log->Printf("%s - '%s' tagged with context 0x%" PRIx64
                  " and script 0x%" PRIx64 ".",
                  __FUNCTION__, strm.GetData(), uint64_t(args[eRsContext]),
                  uint64_t(args[eRsScript]));
  } else if (log) {
    log->Printf("%s - resource name invalid, Script not tagged.", __FUNCTION__);
  }
}

void RenderScriptRuntime::LoadRuntimeHooks(lldb::ModuleSP module,
                                           ModuleKind kind) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!module) {
    return;
  }

  Target &target = GetProcess()->GetTarget();
  const llvm::Triple::ArchType machine = target.GetArchitecture().GetMachine();

  if (machine != llvm::Triple::ArchType::x86 &&
      machine != llvm::Triple::ArchType::arm &&
      machine != llvm::Triple::ArchType::aarch64 &&
      machine != llvm::Triple::ArchType::mipsel &&
      machine != llvm::Triple::ArchType::mips64el &&
      machine != llvm::Triple::ArchType::x86_64) {
    if (log)
      log->Printf("%s - unable to hook runtime functions.", __FUNCTION__);
    return;
  }

  const uint32_t target_ptr_size =
      target.GetArchitecture().GetAddressByteSize();

  std::array<bool, s_runtimeHookCount> hook_placed;
  hook_placed.fill(false);

  for (size_t idx = 0; idx < s_runtimeHookCount; idx++) {
    const HookDefn *hook_defn = &s_runtimeHookDefns[idx];
    if (hook_defn->kind != kind) {
      continue;
    }

    const char *symbol_name = (target_ptr_size == 4)
                                  ? hook_defn->symbol_name_m32
                                  : hook_defn->symbol_name_m64;

    const Symbol *sym = module->FindFirstSymbolWithNameAndType(
        ConstString(symbol_name), eSymbolTypeCode);
    if (!sym) {
      if (log) {
        log->Printf("%s - symbol '%s' related to the function %s not found",
                    __FUNCTION__, symbol_name, hook_defn->name);
      }
      continue;
    }

    addr_t addr = sym->GetLoadAddress(&target);
    if (addr == LLDB_INVALID_ADDRESS) {
      if (log)
        log->Printf("%s - unable to resolve the address of hook function '%s' "
                    "with symbol '%s'.",
                    __FUNCTION__, hook_defn->name, symbol_name);
      continue;
    } else {
      if (log)
        log->Printf("%s - function %s, address resolved at 0x%" PRIx64,
                    __FUNCTION__, hook_defn->name, addr);
    }

    RuntimeHookSP hook(new RuntimeHook());
    hook->address = addr;
    hook->defn = hook_defn;
    hook->bp_sp = target.CreateBreakpoint(addr, true, false);
    hook->bp_sp->SetCallback(HookCallback, hook.get(), true);
    m_runtimeHooks[addr] = hook;
    if (log) {
      log->Printf("%s - successfully hooked '%s' in '%s' version %" PRIu64
                  " at 0x%" PRIx64 ".",
                  __FUNCTION__, hook_defn->name,
                  module->GetFileSpec().GetFilename().AsCString(),
                  (uint64_t)hook_defn->version, (uint64_t)addr);
    }
    hook_placed[idx] = true;
  }

  // log any unhooked function
  if (log) {
    for (size_t i = 0; i < hook_placed.size(); ++i) {
      if (hook_placed[i])
        continue;
      const HookDefn &hook_defn = s_runtimeHookDefns[i];
      if (hook_defn.kind != kind)
        continue;
      log->Printf("%s - function %s was not hooked", __FUNCTION__,
                  hook_defn.name);
    }
  }
}

void RenderScriptRuntime::FixupScriptDetails(RSModuleDescriptorSP rsmodule_sp) {
  if (!rsmodule_sp)
    return;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  const ModuleSP module = rsmodule_sp->m_module;
  const FileSpec &file = module->GetPlatformFileSpec();

  // Iterate over all of the scripts that we currently know of. Note: We cant
  // push or pop to m_scripts here or it may invalidate rs_script.
  for (const auto &rs_script : m_scripts) {
    // Extract the expected .so file path for this script.
    std::string shared_lib;
    if (!rs_script->shared_lib.get(shared_lib))
      continue;

    // Only proceed if the module that has loaded corresponds to this script.
    if (file.GetFilename() != ConstString(shared_lib.c_str()))
      continue;

    // Obtain the script address which we use as a key.
    lldb::addr_t script;
    if (!rs_script->script.get(script))
      continue;

    // If we have a script mapping for the current script.
    if (m_scriptMappings.find(script) != m_scriptMappings.end()) {
      // if the module we have stored is different to the one we just received.
      if (m_scriptMappings[script] != rsmodule_sp) {
        if (log)
          log->Printf(
              "%s - script %" PRIx64 " wants reassigned to new rsmodule '%s'.",
              __FUNCTION__, (uint64_t)script,
              rsmodule_sp->m_module->GetFileSpec().GetFilename().AsCString());
      }
    }
    // We don't have a script mapping for the current script.
    else {
      // Obtain the script resource name.
      std::string res_name;
      if (rs_script->res_name.get(res_name))
        // Set the modules resource name.
        rsmodule_sp->m_resname = res_name;
      // Add Script/Module pair to map.
      m_scriptMappings[script] = rsmodule_sp;
      if (log)
        log->Printf(
            "%s - script %" PRIx64 " associated with rsmodule '%s'.",
            __FUNCTION__, (uint64_t)script,
            rsmodule_sp->m_module->GetFileSpec().GetFilename().AsCString());
    }
  }
}

// Uses the Target API to evaluate the expression passed as a parameter to the
// function The result of that expression is returned an unsigned 64 bit int,
// via the result* parameter. Function returns true on success, and false on
// failure
bool RenderScriptRuntime::EvalRSExpression(const char *expr,
                                           StackFrame *frame_ptr,
                                           uint64_t *result) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));
  if (log)
    log->Printf("%s(%s)", __FUNCTION__, expr);

  ValueObjectSP expr_result;
  EvaluateExpressionOptions options;
  options.SetLanguage(lldb::eLanguageTypeC_plus_plus);
  // Perform the actual expression evaluation
  auto &target = GetProcess()->GetTarget();
  target.EvaluateExpression(expr, frame_ptr, expr_result, options);

  if (!expr_result) {
    if (log)
      log->Printf("%s: couldn't evaluate expression.", __FUNCTION__);
    return false;
  }

  // The result of the expression is invalid
  if (!expr_result->GetError().Success()) {
    Status err = expr_result->GetError();
    // Expression returned is void, so this is actually a success
    if (err.GetError() == UserExpression::kNoResult) {
      if (log)
        log->Printf("%s - expression returned void.", __FUNCTION__);

      result = nullptr;
      return true;
    }

    if (log)
      log->Printf("%s - error evaluating expression result: %s", __FUNCTION__,
                  err.AsCString());
    return false;
  }

  bool success = false;
  // We only read the result as an uint32_t.
  *result = expr_result->GetValueAsUnsigned(0, &success);

  if (!success) {
    if (log)
      log->Printf("%s - couldn't convert expression result to uint32_t",
                  __FUNCTION__);
    return false;
  }

  return true;
}

namespace {
// Used to index expression format strings
enum ExpressionStrings {
  eExprGetOffsetPtr = 0,
  eExprAllocGetType,
  eExprTypeDimX,
  eExprTypeDimY,
  eExprTypeDimZ,
  eExprTypeElemPtr,
  eExprElementType,
  eExprElementKind,
  eExprElementVec,
  eExprElementFieldCount,
  eExprSubelementsId,
  eExprSubelementsName,
  eExprSubelementsArrSize,

  _eExprLast // keep at the end, implicit size of the array runtime_expressions
};

// max length of an expanded expression
const int jit_max_expr_size = 512;

// Retrieve the string to JIT for the given expression
#define JIT_TEMPLATE_CONTEXT "void* ctxt = (void*)rsDebugGetContextWrapper(0x%" PRIx64 "); "
const char *JITTemplate(ExpressionStrings e) {
  // Format strings containing the expressions we may need to evaluate.
  static std::array<const char *, _eExprLast> runtime_expressions = {
      {// Mangled GetOffsetPointer(Allocation*, xoff, yoff, zoff, lod, cubemap)
       "(int*)_"
       "Z12GetOffsetPtrPKN7android12renderscript10AllocationEjjjj23RsAllocation"
       "CubemapFace"
       "(0x%" PRIx64 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", 0, 0)", // eExprGetOffsetPtr

       // Type* rsaAllocationGetType(Context*, Allocation*)
       JIT_TEMPLATE_CONTEXT "(void*)rsaAllocationGetType(ctxt, 0x%" PRIx64 ")", // eExprAllocGetType

       // rsaTypeGetNativeData(Context*, Type*, void* typeData, size) Pack the
       // data in the following way mHal.state.dimX; mHal.state.dimY;
       // mHal.state.dimZ; mHal.state.lodCount; mHal.state.faces; mElement;
       // into typeData Need to specify 32 or 64 bit for uint_t since this
       // differs between devices
       JIT_TEMPLATE_CONTEXT
       "uint%" PRIu32 "_t data[6]; (void*)rsaTypeGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 6); data[0]", // eExprTypeDimX
       JIT_TEMPLATE_CONTEXT
       "uint%" PRIu32 "_t data[6]; (void*)rsaTypeGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 6); data[1]", // eExprTypeDimY
       JIT_TEMPLATE_CONTEXT
       "uint%" PRIu32 "_t data[6]; (void*)rsaTypeGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 6); data[2]", // eExprTypeDimZ
       JIT_TEMPLATE_CONTEXT
       "uint%" PRIu32 "_t data[6]; (void*)rsaTypeGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 6); data[5]", // eExprTypeElemPtr

       // rsaElementGetNativeData(Context*, Element*, uint32_t* elemData,size)
       // Pack mType; mKind; mNormalized; mVectorSize; NumSubElements into
       // elemData
       JIT_TEMPLATE_CONTEXT
       "uint32_t data[5]; (void*)rsaElementGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 5); data[0]", // eExprElementType
       JIT_TEMPLATE_CONTEXT
       "uint32_t data[5]; (void*)rsaElementGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 5); data[1]", // eExprElementKind
       JIT_TEMPLATE_CONTEXT
       "uint32_t data[5]; (void*)rsaElementGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 5); data[3]", // eExprElementVec
       JIT_TEMPLATE_CONTEXT
       "uint32_t data[5]; (void*)rsaElementGetNativeData(ctxt"
       ", 0x%" PRIx64 ", data, 5); data[4]", // eExprElementFieldCount

       // rsaElementGetSubElements(RsContext con, RsElement elem, uintptr_t
       // *ids, const char **names, size_t *arraySizes, uint32_t dataSize)
       // Needed for Allocations of structs to gather details about
       // fields/Subelements Element* of field
       JIT_TEMPLATE_CONTEXT "void* ids[%" PRIu32 "]; const char* names[%" PRIu32
       "]; size_t arr_size[%" PRIu32 "];"
       "(void*)rsaElementGetSubElements(ctxt, 0x%" PRIx64
       ", ids, names, arr_size, %" PRIu32 "); ids[%" PRIu32 "]", // eExprSubelementsId

       // Name of field
       JIT_TEMPLATE_CONTEXT "void* ids[%" PRIu32 "]; const char* names[%" PRIu32
       "]; size_t arr_size[%" PRIu32 "];"
       "(void*)rsaElementGetSubElements(ctxt, 0x%" PRIx64
       ", ids, names, arr_size, %" PRIu32 "); names[%" PRIu32 "]", // eExprSubelementsName

       // Array size of field
       JIT_TEMPLATE_CONTEXT "void* ids[%" PRIu32 "]; const char* names[%" PRIu32
       "]; size_t arr_size[%" PRIu32 "];"
       "(void*)rsaElementGetSubElements(ctxt, 0x%" PRIx64
       ", ids, names, arr_size, %" PRIu32 "); arr_size[%" PRIu32 "]"}}; // eExprSubelementsArrSize

  return runtime_expressions[e];
}
} // end of the anonymous namespace

// JITs the RS runtime for the internal data pointer of an allocation. Is
// passed x,y,z coordinates for the pointer to a specific element. Then sets
// the data_ptr member in Allocation with the result. Returns true on success,
// false otherwise
bool RenderScriptRuntime::JITDataPointer(AllocationDetails *alloc,
                                         StackFrame *frame_ptr, uint32_t x,
                                         uint32_t y, uint32_t z) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!alloc->address.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  const char *fmt_str = JITTemplate(eExprGetOffsetPtr);
  char expr_buf[jit_max_expr_size];

  int written = snprintf(expr_buf, jit_max_expr_size, fmt_str,
                         *alloc->address.get(), x, y, z);
  if (written < 0) {
    if (log)
      log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
    return false;
  } else if (written >= jit_max_expr_size) {
    if (log)
      log->Printf("%s - expression too long.", __FUNCTION__);
    return false;
  }

  uint64_t result = 0;
  if (!EvalRSExpression(expr_buf, frame_ptr, &result))
    return false;

  addr_t data_ptr = static_cast<lldb::addr_t>(result);
  alloc->data_ptr = data_ptr;

  return true;
}

// JITs the RS runtime for the internal pointer to the RS Type of an allocation
// Then sets the type_ptr member in Allocation with the result. Returns true on
// success, false otherwise
bool RenderScriptRuntime::JITTypePointer(AllocationDetails *alloc,
                                         StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!alloc->address.isValid() || !alloc->context.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  const char *fmt_str = JITTemplate(eExprAllocGetType);
  char expr_buf[jit_max_expr_size];

  int written = snprintf(expr_buf, jit_max_expr_size, fmt_str,
                         *alloc->context.get(), *alloc->address.get());
  if (written < 0) {
    if (log)
      log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
    return false;
  } else if (written >= jit_max_expr_size) {
    if (log)
      log->Printf("%s - expression too long.", __FUNCTION__);
    return false;
  }

  uint64_t result = 0;
  if (!EvalRSExpression(expr_buf, frame_ptr, &result))
    return false;

  addr_t type_ptr = static_cast<lldb::addr_t>(result);
  alloc->type_ptr = type_ptr;

  return true;
}

// JITs the RS runtime for information about the dimensions and type of an
// allocation Then sets dimension and element_ptr members in Allocation with
// the result. Returns true on success, false otherwise
bool RenderScriptRuntime::JITTypePacked(AllocationDetails *alloc,
                                        StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!alloc->type_ptr.isValid() || !alloc->context.isValid()) {
    if (log)
      log->Printf("%s - Failed to find allocation details.", __FUNCTION__);
    return false;
  }

  // Expression is different depending on if device is 32 or 64 bit
  uint32_t target_ptr_size =
      GetProcess()->GetTarget().GetArchitecture().GetAddressByteSize();
  const uint32_t bits = target_ptr_size == 4 ? 32 : 64;

  // We want 4 elements from packed data
  const uint32_t num_exprs = 4;
  assert(num_exprs == (eExprTypeElemPtr - eExprTypeDimX + 1) &&
         "Invalid number of expressions");

  char expr_bufs[num_exprs][jit_max_expr_size];
  uint64_t results[num_exprs];

  for (uint32_t i = 0; i < num_exprs; ++i) {
    const char *fmt_str = JITTemplate(ExpressionStrings(eExprTypeDimX + i));
    int written = snprintf(expr_bufs[i], jit_max_expr_size, fmt_str,
                           *alloc->context.get(), bits, *alloc->type_ptr.get());
    if (written < 0) {
      if (log)
        log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
      return false;
    } else if (written >= jit_max_expr_size) {
      if (log)
        log->Printf("%s - expression too long.", __FUNCTION__);
      return false;
    }

    // Perform expression evaluation
    if (!EvalRSExpression(expr_bufs[i], frame_ptr, &results[i]))
      return false;
  }

  // Assign results to allocation members
  AllocationDetails::Dimension dims;
  dims.dim_1 = static_cast<uint32_t>(results[0]);
  dims.dim_2 = static_cast<uint32_t>(results[1]);
  dims.dim_3 = static_cast<uint32_t>(results[2]);
  alloc->dimension = dims;

  addr_t element_ptr = static_cast<lldb::addr_t>(results[3]);
  alloc->element.element_ptr = element_ptr;

  if (log)
    log->Printf("%s - dims (%" PRIu32 ", %" PRIu32 ", %" PRIu32
                ") Element*: 0x%" PRIx64 ".",
                __FUNCTION__, dims.dim_1, dims.dim_2, dims.dim_3, element_ptr);

  return true;
}

// JITs the RS runtime for information about the Element of an allocation Then
// sets type, type_vec_size, field_count and type_kind members in Element with
// the result. Returns true on success, false otherwise
bool RenderScriptRuntime::JITElementPacked(Element &elem,
                                           const lldb::addr_t context,
                                           StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!elem.element_ptr.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  // We want 4 elements from packed data
  const uint32_t num_exprs = 4;
  assert(num_exprs == (eExprElementFieldCount - eExprElementType + 1) &&
         "Invalid number of expressions");

  char expr_bufs[num_exprs][jit_max_expr_size];
  uint64_t results[num_exprs];

  for (uint32_t i = 0; i < num_exprs; i++) {
    const char *fmt_str = JITTemplate(ExpressionStrings(eExprElementType + i));
    int written = snprintf(expr_bufs[i], jit_max_expr_size, fmt_str, context,
                           *elem.element_ptr.get());
    if (written < 0) {
      if (log)
        log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
      return false;
    } else if (written >= jit_max_expr_size) {
      if (log)
        log->Printf("%s - expression too long.", __FUNCTION__);
      return false;
    }

    // Perform expression evaluation
    if (!EvalRSExpression(expr_bufs[i], frame_ptr, &results[i]))
      return false;
  }

  // Assign results to allocation members
  elem.type = static_cast<RenderScriptRuntime::Element::DataType>(results[0]);
  elem.type_kind =
      static_cast<RenderScriptRuntime::Element::DataKind>(results[1]);
  elem.type_vec_size = static_cast<uint32_t>(results[2]);
  elem.field_count = static_cast<uint32_t>(results[3]);

  if (log)
    log->Printf("%s - data type %" PRIu32 ", pixel type %" PRIu32
                ", vector size %" PRIu32 ", field count %" PRIu32,
                __FUNCTION__, *elem.type.get(), *elem.type_kind.get(),
                *elem.type_vec_size.get(), *elem.field_count.get());

  // If this Element has subelements then JIT rsaElementGetSubElements() for
  // details about its fields
  return !(*elem.field_count.get() > 0 &&
           !JITSubelements(elem, context, frame_ptr));
}

// JITs the RS runtime for information about the subelements/fields of a struct
// allocation This is necessary for infering the struct type so we can pretty
// print the allocation's contents. Returns true on success, false otherwise
bool RenderScriptRuntime::JITSubelements(Element &elem,
                                         const lldb::addr_t context,
                                         StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!elem.element_ptr.isValid() || !elem.field_count.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  const short num_exprs = 3;
  assert(num_exprs == (eExprSubelementsArrSize - eExprSubelementsId + 1) &&
         "Invalid number of expressions");

  char expr_buffer[jit_max_expr_size];
  uint64_t results;

  // Iterate over struct fields.
  const uint32_t field_count = *elem.field_count.get();
  for (uint32_t field_index = 0; field_index < field_count; ++field_index) {
    Element child;
    for (uint32_t expr_index = 0; expr_index < num_exprs; ++expr_index) {
      const char *fmt_str =
          JITTemplate(ExpressionStrings(eExprSubelementsId + expr_index));
      int written = snprintf(expr_buffer, jit_max_expr_size, fmt_str,
                             context, field_count, field_count, field_count,
                             *elem.element_ptr.get(), field_count, field_index);
      if (written < 0) {
        if (log)
          log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
        return false;
      } else if (written >= jit_max_expr_size) {
        if (log)
          log->Printf("%s - expression too long.", __FUNCTION__);
        return false;
      }

      // Perform expression evaluation
      if (!EvalRSExpression(expr_buffer, frame_ptr, &results))
        return false;

      if (log)
        log->Printf("%s - expr result 0x%" PRIx64 ".", __FUNCTION__, results);

      switch (expr_index) {
      case 0: // Element* of child
        child.element_ptr = static_cast<addr_t>(results);
        break;
      case 1: // Name of child
      {
        lldb::addr_t address = static_cast<addr_t>(results);
        Status err;
        std::string name;
        GetProcess()->ReadCStringFromMemory(address, name, err);
        if (!err.Fail())
          child.type_name = ConstString(name);
        else {
          if (log)
            log->Printf("%s - warning: Couldn't read field name.",
                        __FUNCTION__);
        }
        break;
      }
      case 2: // Array size of child
        child.array_size = static_cast<uint32_t>(results);
        break;
      }
    }

    // We need to recursively JIT each Element field of the struct since
    // structs can be nested inside structs.
    if (!JITElementPacked(child, context, frame_ptr))
      return false;
    elem.children.push_back(child);
  }

  // Try to infer the name of the struct type so we can pretty print the
  // allocation contents.
  FindStructTypeName(elem, frame_ptr);

  return true;
}

// JITs the RS runtime for the address of the last element in the allocation.
// The `elem_size` parameter represents the size of a single element, including
// padding. Which is needed as an offset from the last element pointer. Using
// this offset minus the starting address we can calculate the size of the
// allocation. Returns true on success, false otherwise
bool RenderScriptRuntime::JITAllocationSize(AllocationDetails *alloc,
                                            StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!alloc->address.isValid() || !alloc->dimension.isValid() ||
      !alloc->data_ptr.isValid() || !alloc->element.datum_size.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  // Find dimensions
  uint32_t dim_x = alloc->dimension.get()->dim_1;
  uint32_t dim_y = alloc->dimension.get()->dim_2;
  uint32_t dim_z = alloc->dimension.get()->dim_3;

  // Our plan of jitting the last element address doesn't seem to work for
  // struct Allocations` Instead try to infer the size ourselves without any
  // inter element padding.
  if (alloc->element.children.size() > 0) {
    if (dim_x == 0)
      dim_x = 1;
    if (dim_y == 0)
      dim_y = 1;
    if (dim_z == 0)
      dim_z = 1;

    alloc->size = dim_x * dim_y * dim_z * *alloc->element.datum_size.get();

    if (log)
      log->Printf("%s - inferred size of struct allocation %" PRIu32 ".",
                  __FUNCTION__, *alloc->size.get());
    return true;
  }

  const char *fmt_str = JITTemplate(eExprGetOffsetPtr);
  char expr_buf[jit_max_expr_size];

  // Calculate last element
  dim_x = dim_x == 0 ? 0 : dim_x - 1;
  dim_y = dim_y == 0 ? 0 : dim_y - 1;
  dim_z = dim_z == 0 ? 0 : dim_z - 1;

  int written = snprintf(expr_buf, jit_max_expr_size, fmt_str,
                         *alloc->address.get(), dim_x, dim_y, dim_z);
  if (written < 0) {
    if (log)
      log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
    return false;
  } else if (written >= jit_max_expr_size) {
    if (log)
      log->Printf("%s - expression too long.", __FUNCTION__);
    return false;
  }

  uint64_t result = 0;
  if (!EvalRSExpression(expr_buf, frame_ptr, &result))
    return false;

  addr_t mem_ptr = static_cast<lldb::addr_t>(result);
  // Find pointer to last element and add on size of an element
  alloc->size = static_cast<uint32_t>(mem_ptr - *alloc->data_ptr.get()) +
                *alloc->element.datum_size.get();

  return true;
}

// JITs the RS runtime for information about the stride between rows in the
// allocation. This is done to detect padding, since allocated memory is
// 16-byte aligned. Returns true on success, false otherwise
bool RenderScriptRuntime::JITAllocationStride(AllocationDetails *alloc,
                                              StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!alloc->address.isValid() || !alloc->data_ptr.isValid()) {
    if (log)
      log->Printf("%s - failed to find allocation details.", __FUNCTION__);
    return false;
  }

  const char *fmt_str = JITTemplate(eExprGetOffsetPtr);
  char expr_buf[jit_max_expr_size];

  int written = snprintf(expr_buf, jit_max_expr_size, fmt_str,
                         *alloc->address.get(), 0, 1, 0);
  if (written < 0) {
    if (log)
      log->Printf("%s - encoding error in snprintf().", __FUNCTION__);
    return false;
  } else if (written >= jit_max_expr_size) {
    if (log)
      log->Printf("%s - expression too long.", __FUNCTION__);
    return false;
  }

  uint64_t result = 0;
  if (!EvalRSExpression(expr_buf, frame_ptr, &result))
    return false;

  addr_t mem_ptr = static_cast<lldb::addr_t>(result);
  alloc->stride = static_cast<uint32_t>(mem_ptr - *alloc->data_ptr.get());

  return true;
}

// JIT all the current runtime info regarding an allocation
bool RenderScriptRuntime::RefreshAllocation(AllocationDetails *alloc,
                                            StackFrame *frame_ptr) {
  // GetOffsetPointer()
  if (!JITDataPointer(alloc, frame_ptr))
    return false;

  // rsaAllocationGetType()
  if (!JITTypePointer(alloc, frame_ptr))
    return false;

  // rsaTypeGetNativeData()
  if (!JITTypePacked(alloc, frame_ptr))
    return false;

  // rsaElementGetNativeData()
  if (!JITElementPacked(alloc->element, *alloc->context.get(), frame_ptr))
    return false;

  // Sets the datum_size member in Element
  SetElementSize(alloc->element);

  // Use GetOffsetPointer() to infer size of the allocation
  return JITAllocationSize(alloc, frame_ptr);
}

// Function attempts to set the type_name member of the paramaterised Element
// object. This string should be the name of the struct type the Element
// represents. We need this string for pretty printing the Element to users.
void RenderScriptRuntime::FindStructTypeName(Element &elem,
                                             StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!elem.type_name.IsEmpty()) // Name already set
    return;
  else
    elem.type_name = Element::GetFallbackStructName(); // Default type name if
                                                       // we don't succeed

  // Find all the global variables from the script rs modules
  VariableList var_list;
  for (auto module_sp : m_rsmodules)
    module_sp->m_module->FindGlobalVariables(
        RegularExpression(llvm::StringRef(".")), UINT32_MAX, var_list);

  // Iterate over all the global variables looking for one with a matching type
  // to the Element. We make the assumption a match exists since there needs to
  // be a global variable to reflect the struct type back into java host code.
  for (uint32_t i = 0; i < var_list.GetSize(); ++i) {
    const VariableSP var_sp(var_list.GetVariableAtIndex(i));
    if (!var_sp)
      continue;

    ValueObjectSP valobj_sp = ValueObjectVariable::Create(frame_ptr, var_sp);
    if (!valobj_sp)
      continue;

    // Find the number of variable fields.
    // If it has no fields, or more fields than our Element, then it can't be
    // the struct we're looking for. Don't check for equality since RS can add
    // extra struct members for padding.
    size_t num_children = valobj_sp->GetNumChildren();
    if (num_children > elem.children.size() || num_children == 0)
      continue;

    // Iterate over children looking for members with matching field names. If
    // all the field names match, this is likely the struct we want.
    //   TODO: This could be made more robust by also checking children data
    //   sizes, or array size
    bool found = true;
    for (size_t i = 0; i < num_children; ++i) {
      ValueObjectSP child = valobj_sp->GetChildAtIndex(i, true);
      if (!child || (child->GetName() != elem.children[i].type_name)) {
        found = false;
        break;
      }
    }

    // RS can add extra struct members for padding in the format
    // '#rs_padding_[0-9]+'
    if (found && num_children < elem.children.size()) {
      const uint32_t size_diff = elem.children.size() - num_children;
      if (log)
        log->Printf("%s - %" PRIu32 " padding struct entries", __FUNCTION__,
                    size_diff);

      for (uint32_t i = 0; i < size_diff; ++i) {
        const ConstString &name = elem.children[num_children + i].type_name;
        if (strcmp(name.AsCString(), "#rs_padding") < 0)
          found = false;
      }
    }

    // We've found a global variable with matching type
    if (found) {
      // Dereference since our Element type isn't a pointer.
      if (valobj_sp->IsPointerType()) {
        Status err;
        ValueObjectSP deref_valobj = valobj_sp->Dereference(err);
        if (!err.Fail())
          valobj_sp = deref_valobj;
      }

      // Save name of variable in Element.
      elem.type_name = valobj_sp->GetTypeName();
      if (log)
        log->Printf("%s - element name set to %s", __FUNCTION__,
                    elem.type_name.AsCString());

      return;
    }
  }
}

// Function sets the datum_size member of Element. Representing the size of a
// single instance including padding. Assumes the relevant allocation
// information has already been jitted.
void RenderScriptRuntime::SetElementSize(Element &elem) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));
  const Element::DataType type = *elem.type.get();
  assert(type >= Element::RS_TYPE_NONE && type <= Element::RS_TYPE_FONT &&
         "Invalid allocation type");

  const uint32_t vec_size = *elem.type_vec_size.get();
  uint32_t data_size = 0;
  uint32_t padding = 0;

  // Element is of a struct type, calculate size recursively.
  if ((type == Element::RS_TYPE_NONE) && (elem.children.size() > 0)) {
    for (Element &child : elem.children) {
      SetElementSize(child);
      const uint32_t array_size =
          child.array_size.isValid() ? *child.array_size.get() : 1;
      data_size += *child.datum_size.get() * array_size;
    }
  }
  // These have been packed already
  else if (type == Element::RS_TYPE_UNSIGNED_5_6_5 ||
           type == Element::RS_TYPE_UNSIGNED_5_5_5_1 ||
           type == Element::RS_TYPE_UNSIGNED_4_4_4_4) {
    data_size = AllocationDetails::RSTypeToFormat[type][eElementSize];
  } else if (type < Element::RS_TYPE_ELEMENT) {
    data_size =
        vec_size * AllocationDetails::RSTypeToFormat[type][eElementSize];
    if (vec_size == 3)
      padding = AllocationDetails::RSTypeToFormat[type][eElementSize];
  } else
    data_size =
        GetProcess()->GetTarget().GetArchitecture().GetAddressByteSize();

  elem.padding = padding;
  elem.datum_size = data_size + padding;
  if (log)
    log->Printf("%s - element size set to %" PRIu32, __FUNCTION__,
                data_size + padding);
}

// Given an allocation, this function copies the allocation contents from
// device into a buffer on the heap. Returning a shared pointer to the buffer
// containing the data.
std::shared_ptr<uint8_t>
RenderScriptRuntime::GetAllocationData(AllocationDetails *alloc,
                                       StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  // JIT all the allocation details
  if (alloc->ShouldRefresh()) {
    if (log)
      log->Printf("%s - allocation details not calculated yet, jitting info",
                  __FUNCTION__);

    if (!RefreshAllocation(alloc, frame_ptr)) {
      if (log)
        log->Printf("%s - couldn't JIT allocation details", __FUNCTION__);
      return nullptr;
    }
  }

  assert(alloc->data_ptr.isValid() && alloc->element.type.isValid() &&
         alloc->element.type_vec_size.isValid() && alloc->size.isValid() &&
         "Allocation information not available");

  // Allocate a buffer to copy data into
  const uint32_t size = *alloc->size.get();
  std::shared_ptr<uint8_t> buffer(new uint8_t[size]);
  if (!buffer) {
    if (log)
      log->Printf("%s - couldn't allocate a %" PRIu32 " byte buffer",
                  __FUNCTION__, size);
    return nullptr;
  }

  // Read the inferior memory
  Status err;
  lldb::addr_t data_ptr = *alloc->data_ptr.get();
  GetProcess()->ReadMemory(data_ptr, buffer.get(), size, err);
  if (err.Fail()) {
    if (log)
      log->Printf("%s - '%s' Couldn't read %" PRIu32
                  " bytes of allocation data from 0x%" PRIx64,
                  __FUNCTION__, err.AsCString(), size, data_ptr);
    return nullptr;
  }

  return buffer;
}

// Function copies data from a binary file into an allocation. There is a
// header at the start of the file, FileHeader, before the data content itself.
// Information from this header is used to display warnings to the user about
// incompatibilities
bool RenderScriptRuntime::LoadAllocation(Stream &strm, const uint32_t alloc_id,
                                         const char *path,
                                         StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  // Find allocation with the given id
  AllocationDetails *alloc = FindAllocByID(strm, alloc_id);
  if (!alloc)
    return false;

  if (log)
    log->Printf("%s - found allocation 0x%" PRIx64, __FUNCTION__,
                *alloc->address.get());

  // JIT all the allocation details
  if (alloc->ShouldRefresh()) {
    if (log)
      log->Printf("%s - allocation details not calculated yet, jitting info.",
                  __FUNCTION__);

    if (!RefreshAllocation(alloc, frame_ptr)) {
      if (log)
        log->Printf("%s - couldn't JIT allocation details", __FUNCTION__);
      return false;
    }
  }

  assert(alloc->data_ptr.isValid() && alloc->element.type.isValid() &&
         alloc->element.type_vec_size.isValid() && alloc->size.isValid() &&
         alloc->element.datum_size.isValid() &&
         "Allocation information not available");

  // Check we can read from file
  FileSpec file(path);
  FileSystem::Instance().Resolve(file);
  if (!FileSystem::Instance().Exists(file)) {
    strm.Printf("Error: File %s does not exist", path);
    strm.EOL();
    return false;
  }

  if (!FileSystem::Instance().Readable(file)) {
    strm.Printf("Error: File %s does not have readable permissions", path);
    strm.EOL();
    return false;
  }

  // Read file into data buffer
  auto data_sp = FileSystem::Instance().CreateDataBuffer(file.GetPath());

  // Cast start of buffer to FileHeader and use pointer to read metadata
  void *file_buf = data_sp->GetBytes();
  if (file_buf == nullptr ||
      data_sp->GetByteSize() < (sizeof(AllocationDetails::FileHeader) +
                                sizeof(AllocationDetails::ElementHeader))) {
    strm.Printf("Error: File %s does not contain enough data for header", path);
    strm.EOL();
    return false;
  }
  const AllocationDetails::FileHeader *file_header =
      static_cast<AllocationDetails::FileHeader *>(file_buf);

  // Check file starts with ascii characters "RSAD"
  if (memcmp(file_header->ident, "RSAD", 4)) {
    strm.Printf("Error: File doesn't contain identifier for an RS allocation "
                "dump. Are you sure this is the correct file?");
    strm.EOL();
    return false;
  }

  // Look at the type of the root element in the header
  AllocationDetails::ElementHeader root_el_hdr;
  memcpy(&root_el_hdr, static_cast<uint8_t *>(file_buf) +
                           sizeof(AllocationDetails::FileHeader),
         sizeof(AllocationDetails::ElementHeader));

  if (log)
    log->Printf("%s - header type %" PRIu32 ", element size %" PRIu32,
                __FUNCTION__, root_el_hdr.type, root_el_hdr.element_size);

  // Check if the target allocation and file both have the same number of bytes
  // for an Element
  if (*alloc->element.datum_size.get() != root_el_hdr.element_size) {
    strm.Printf("Warning: Mismatched Element sizes - file %" PRIu32
                " bytes, allocation %" PRIu32 " bytes",
                root_el_hdr.element_size, *alloc->element.datum_size.get());
    strm.EOL();
  }

  // Check if the target allocation and file both have the same type
  const uint32_t alloc_type = static_cast<uint32_t>(*alloc->element.type.get());
  const uint32_t file_type = root_el_hdr.type;

  if (file_type > Element::RS_TYPE_FONT) {
    strm.Printf("Warning: File has unknown allocation type");
    strm.EOL();
  } else if (alloc_type != file_type) {
    // Enum value isn't monotonous, so doesn't always index RsDataTypeToString
    // array
    uint32_t target_type_name_idx = alloc_type;
    uint32_t head_type_name_idx = file_type;
    if (alloc_type >= Element::RS_TYPE_ELEMENT &&
        alloc_type <= Element::RS_TYPE_FONT)
      target_type_name_idx = static_cast<Element::DataType>(
          (alloc_type - Element::RS_TYPE_ELEMENT) +
          Element::RS_TYPE_MATRIX_2X2 + 1);

    if (file_type >= Element::RS_TYPE_ELEMENT &&
        file_type <= Element::RS_TYPE_FONT)
      head_type_name_idx = static_cast<Element::DataType>(
          (file_type - Element::RS_TYPE_ELEMENT) + Element::RS_TYPE_MATRIX_2X2 +
          1);

    const char *head_type_name =
        AllocationDetails::RsDataTypeToString[head_type_name_idx][0];
    const char *target_type_name =
        AllocationDetails::RsDataTypeToString[target_type_name_idx][0];

    strm.Printf(
        "Warning: Mismatched Types - file '%s' type, allocation '%s' type",
        head_type_name, target_type_name);
    strm.EOL();
  }

  // Advance buffer past header
  file_buf = static_cast<uint8_t *>(file_buf) + file_header->hdr_size;

  // Calculate size of allocation data in file
  size_t size = data_sp->GetByteSize() - file_header->hdr_size;

  // Check if the target allocation and file both have the same total data
  // size.
  const uint32_t alloc_size = *alloc->size.get();
  if (alloc_size != size) {
    strm.Printf("Warning: Mismatched allocation sizes - file 0x%" PRIx64
                " bytes, allocation 0x%" PRIx32 " bytes",
                (uint64_t)size, alloc_size);
    strm.EOL();
    // Set length to copy to minimum
    size = alloc_size < size ? alloc_size : size;
  }

  // Copy file data from our buffer into the target allocation.
  lldb::addr_t alloc_data = *alloc->data_ptr.get();
  Status err;
  size_t written = GetProcess()->WriteMemory(alloc_data, file_buf, size, err);
  if (!err.Success() || written != size) {
    strm.Printf("Error: Couldn't write data to allocation %s", err.AsCString());
    strm.EOL();
    return false;
  }

  strm.Printf("Contents of file '%s' read into allocation %" PRIu32, path,
              alloc->id);
  strm.EOL();

  return true;
}

// Function takes as parameters a byte buffer, which will eventually be written
// to file as the element header, an offset into that buffer, and an Element
// that will be saved into the buffer at the parametrised offset. Return value
// is the new offset after writing the element into the buffer. Elements are
// saved to the file as the ElementHeader struct followed by offsets to the
// structs of all the element's children.
size_t RenderScriptRuntime::PopulateElementHeaders(
    const std::shared_ptr<uint8_t> header_buffer, size_t offset,
    const Element &elem) {
  // File struct for an element header with all the relevant details copied
  // from elem. We assume members are valid already.
  AllocationDetails::ElementHeader elem_header;
  elem_header.type = *elem.type.get();
  elem_header.kind = *elem.type_kind.get();
  elem_header.element_size = *elem.datum_size.get();
  elem_header.vector_size = *elem.type_vec_size.get();
  elem_header.array_size =
      elem.array_size.isValid() ? *elem.array_size.get() : 0;
  const size_t elem_header_size = sizeof(AllocationDetails::ElementHeader);

  // Copy struct into buffer and advance offset We assume that header_buffer
  // has been checked for nullptr before this method is called
  memcpy(header_buffer.get() + offset, &elem_header, elem_header_size);
  offset += elem_header_size;

  // Starting offset of child ElementHeader struct
  size_t child_offset =
      offset + ((elem.children.size() + 1) * sizeof(uint32_t));
  for (const RenderScriptRuntime::Element &child : elem.children) {
    // Recursively populate the buffer with the element header structs of
    // children. Then save the offsets where they were set after the parent
    // element header.
    memcpy(header_buffer.get() + offset, &child_offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    child_offset = PopulateElementHeaders(header_buffer, child_offset, child);
  }

  // Zero indicates no more children
  memset(header_buffer.get() + offset, 0, sizeof(uint32_t));

  return child_offset;
}

// Given an Element object this function returns the total size needed in the
// file header to store the element's details. Taking into account the size of
// the element header struct, plus the offsets to all the element's children.
// Function is recursive so that the size of all ancestors is taken into
// account.
size_t RenderScriptRuntime::CalculateElementHeaderSize(const Element &elem) {
  // Offsets to children plus zero terminator
  size_t size = (elem.children.size() + 1) * sizeof(uint32_t);
  // Size of header struct with type details
  size += sizeof(AllocationDetails::ElementHeader);

  // Calculate recursively for all descendants
  for (const Element &child : elem.children)
    size += CalculateElementHeaderSize(child);

  return size;
}

// Function copies allocation contents into a binary file. This file can then
// be loaded later into a different allocation. There is a header, FileHeader,
// before the allocation data containing meta-data.
bool RenderScriptRuntime::SaveAllocation(Stream &strm, const uint32_t alloc_id,
                                         const char *path,
                                         StackFrame *frame_ptr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  // Find allocation with the given id
  AllocationDetails *alloc = FindAllocByID(strm, alloc_id);
  if (!alloc)
    return false;

  if (log)
    log->Printf("%s - found allocation 0x%" PRIx64 ".", __FUNCTION__,
                *alloc->address.get());

  // JIT all the allocation details
  if (alloc->ShouldRefresh()) {
    if (log)
      log->Printf("%s - allocation details not calculated yet, jitting info.",
                  __FUNCTION__);

    if (!RefreshAllocation(alloc, frame_ptr)) {
      if (log)
        log->Printf("%s - couldn't JIT allocation details.", __FUNCTION__);
      return false;
    }
  }

  assert(alloc->data_ptr.isValid() && alloc->element.type.isValid() &&
         alloc->element.type_vec_size.isValid() &&
         alloc->element.datum_size.get() &&
         alloc->element.type_kind.isValid() && alloc->dimension.isValid() &&
         "Allocation information not available");

  // Check we can create writable file
  FileSpec file_spec(path);
  FileSystem::Instance().Resolve(file_spec);
  File file;
  FileSystem::Instance().Open(file, file_spec,
                              File::eOpenOptionWrite |
                                  File::eOpenOptionCanCreate |
                                  File::eOpenOptionTruncate);

  if (!file) {
    strm.Printf("Error: Failed to open '%s' for writing", path);
    strm.EOL();
    return false;
  }

  // Read allocation into buffer of heap memory
  const std::shared_ptr<uint8_t> buffer = GetAllocationData(alloc, frame_ptr);
  if (!buffer) {
    strm.Printf("Error: Couldn't read allocation data into buffer");
    strm.EOL();
    return false;
  }

  // Create the file header
  AllocationDetails::FileHeader head;
  memcpy(head.ident, "RSAD", 4);
  head.dims[0] = static_cast<uint32_t>(alloc->dimension.get()->dim_1);
  head.dims[1] = static_cast<uint32_t>(alloc->dimension.get()->dim_2);
  head.dims[2] = static_cast<uint32_t>(alloc->dimension.get()->dim_3);

  const size_t element_header_size = CalculateElementHeaderSize(alloc->element);
  assert((sizeof(AllocationDetails::FileHeader) + element_header_size) <
             UINT16_MAX &&
         "Element header too large");
  head.hdr_size = static_cast<uint16_t>(sizeof(AllocationDetails::FileHeader) +
                                        element_header_size);

  // Write the file header
  size_t num_bytes = sizeof(AllocationDetails::FileHeader);
  if (log)
    log->Printf("%s - writing File Header, 0x%" PRIx64 " bytes", __FUNCTION__,
                (uint64_t)num_bytes);

  Status err = file.Write(&head, num_bytes);
  if (!err.Success()) {
    strm.Printf("Error: '%s' when writing to file '%s'", err.AsCString(), path);
    strm.EOL();
    return false;
  }

  // Create the headers describing the element type of the allocation.
  std::shared_ptr<uint8_t> element_header_buffer(
      new uint8_t[element_header_size]);
  if (element_header_buffer == nullptr) {
    strm.Printf("Internal Error: Couldn't allocate %" PRIu64
                " bytes on the heap",
                (uint64_t)element_header_size);
    strm.EOL();
    return false;
  }

  PopulateElementHeaders(element_header_buffer, 0, alloc->element);

  // Write headers for allocation element type to file
  num_bytes = element_header_size;
  if (log)
    log->Printf("%s - writing element headers, 0x%" PRIx64 " bytes.",
                __FUNCTION__, (uint64_t)num_bytes);

  err = file.Write(element_header_buffer.get(), num_bytes);
  if (!err.Success()) {
    strm.Printf("Error: '%s' when writing to file '%s'", err.AsCString(), path);
    strm.EOL();
    return false;
  }

  // Write allocation data to file
  num_bytes = static_cast<size_t>(*alloc->size.get());
  if (log)
    log->Printf("%s - writing 0x%" PRIx64 " bytes", __FUNCTION__,
                (uint64_t)num_bytes);

  err = file.Write(buffer.get(), num_bytes);
  if (!err.Success()) {
    strm.Printf("Error: '%s' when writing to file '%s'", err.AsCString(), path);
    strm.EOL();
    return false;
  }

  strm.Printf("Allocation written to file '%s'", path);
  strm.EOL();
  return true;
}

bool RenderScriptRuntime::LoadModule(const lldb::ModuleSP &module_sp) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (module_sp) {
    for (const auto &rs_module : m_rsmodules) {
      if (rs_module->m_module == module_sp) {
        // Check if the user has enabled automatically breaking on all RS
        // kernels.
        if (m_breakAllKernels)
          BreakOnModuleKernels(rs_module);

        return false;
      }
    }
    bool module_loaded = false;
    switch (GetModuleKind(module_sp)) {
    case eModuleKindKernelObj: {
      RSModuleDescriptorSP module_desc;
      module_desc.reset(new RSModuleDescriptor(module_sp));
      if (module_desc->ParseRSInfo()) {
        m_rsmodules.push_back(module_desc);
        module_desc->WarnIfVersionMismatch(GetProcess()
                                               ->GetTarget()
                                               .GetDebugger()
                                               .GetAsyncOutputStream()
                                               .get());
        module_loaded = true;
      }
      if (module_loaded) {
        FixupScriptDetails(module_desc);
      }
      break;
    }
    case eModuleKindDriver: {
      if (!m_libRSDriver) {
        m_libRSDriver = module_sp;
        LoadRuntimeHooks(m_libRSDriver, RenderScriptRuntime::eModuleKindDriver);
      }
      break;
    }
    case eModuleKindImpl: {
      if (!m_libRSCpuRef) {
        m_libRSCpuRef = module_sp;
        LoadRuntimeHooks(m_libRSCpuRef, RenderScriptRuntime::eModuleKindImpl);
      }
      break;
    }
    case eModuleKindLibRS: {
      if (!m_libRS) {
        m_libRS = module_sp;
        static ConstString gDbgPresentStr("gDebuggerPresent");
        const Symbol *debug_present = m_libRS->FindFirstSymbolWithNameAndType(
            gDbgPresentStr, eSymbolTypeData);
        if (debug_present) {
          Status err;
          uint32_t flag = 0x00000001U;
          Target &target = GetProcess()->GetTarget();
          addr_t addr = debug_present->GetLoadAddress(&target);
          GetProcess()->WriteMemory(addr, &flag, sizeof(flag), err);
          if (err.Success()) {
            if (log)
              log->Printf("%s - debugger present flag set on debugee.",
                          __FUNCTION__);

            m_debuggerPresentFlagged = true;
          } else if (log) {
            log->Printf("%s - error writing debugger present flags '%s' ",
                        __FUNCTION__, err.AsCString());
          }
        } else if (log) {
          log->Printf(
              "%s - error writing debugger present flags - symbol not found",
              __FUNCTION__);
        }
      }
      break;
    }
    default:
      break;
    }
    if (module_loaded)
      Update();
    return module_loaded;
  }
  return false;
}

void RenderScriptRuntime::Update() {
  if (m_rsmodules.size() > 0) {
    if (!m_initiated) {
      Initiate();
    }
  }
}

void RSModuleDescriptor::WarnIfVersionMismatch(lldb_private::Stream *s) const {
  if (!s)
    return;

  if (m_slang_version.empty() || m_bcc_version.empty()) {
    s->PutCString("WARNING: Unknown bcc or slang (llvm-rs-cc) version; debug "
                  "experience may be unreliable");
    s->EOL();
  } else if (m_slang_version != m_bcc_version) {
    s->Printf("WARNING: The debug info emitted by the slang frontend "
              "(llvm-rs-cc) used to build this module (%s) does not match the "
              "version of bcc used to generate the debug information (%s). "
              "This is an unsupported configuration and may result in a poor "
              "debugging experience; proceed with caution",
              m_slang_version.c_str(), m_bcc_version.c_str());
    s->EOL();
  }
}

bool RSModuleDescriptor::ParsePragmaCount(llvm::StringRef *lines,
                                          size_t n_lines) {
  // Skip the pragma prototype line
  ++lines;
  for (; n_lines--; ++lines) {
    const auto kv_pair = lines->split(" - ");
    m_pragmas[kv_pair.first.trim().str()] = kv_pair.second.trim().str();
  }
  return true;
}

bool RSModuleDescriptor::ParseExportReduceCount(llvm::StringRef *lines,
                                                size_t n_lines) {
  // The list of reduction kernels in the `.rs.info` symbol is of the form
  // "signature - accumulatordatasize - reduction_name - initializer_name -
  // accumulator_name - combiner_name - outconverter_name - halter_name" Where
  // a function is not explicitly named by the user, or is not generated by the
  // compiler, it is named "." so the dash separated list should always be 8
  // items long
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);
  // Skip the exportReduceCount line
  ++lines;
  for (; n_lines--; ++lines) {
    llvm::SmallVector<llvm::StringRef, 8> spec;
    lines->split(spec, " - ");
    if (spec.size() != 8) {
      if (spec.size() < 8) {
        if (log)
          log->Error("Error parsing RenderScript reduction spec. wrong number "
                     "of fields");
        return false;
      } else if (log)
        log->Warning("Extraneous members in reduction spec: '%s'",
                     lines->str().c_str());
    }

    const auto sig_s = spec[0];
    uint32_t sig;
    if (sig_s.getAsInteger(10, sig)) {
      if (log)
        log->Error("Error parsing Renderscript reduction spec: invalid kernel "
                   "signature: '%s'",
                   sig_s.str().c_str());
      return false;
    }

    const auto accum_data_size_s = spec[1];
    uint32_t accum_data_size;
    if (accum_data_size_s.getAsInteger(10, accum_data_size)) {
      if (log)
        log->Error("Error parsing Renderscript reduction spec: invalid "
                   "accumulator data size %s",
                   accum_data_size_s.str().c_str());
      return false;
    }

    if (log)
      log->Printf("Found RenderScript reduction '%s'", spec[2].str().c_str());

    m_reductions.push_back(RSReductionDescriptor(this, sig, accum_data_size,
                                                 spec[2], spec[3], spec[4],
                                                 spec[5], spec[6], spec[7]));
  }
  return true;
}

bool RSModuleDescriptor::ParseVersionInfo(llvm::StringRef *lines,
                                          size_t n_lines) {
  // Skip the versionInfo line
  ++lines;
  for (; n_lines--; ++lines) {
    // We're only interested in bcc and slang versions, and ignore all other
    // versionInfo lines
    const auto kv_pair = lines->split(" - ");
    if (kv_pair.first == "slang")
      m_slang_version = kv_pair.second.str();
    else if (kv_pair.first == "bcc")
      m_bcc_version = kv_pair.second.str();
  }
  return true;
}

bool RSModuleDescriptor::ParseExportForeachCount(llvm::StringRef *lines,
                                                 size_t n_lines) {
  // Skip the exportForeachCount line
  ++lines;
  for (; n_lines--; ++lines) {
    uint32_t slot;
    // `forEach` kernels are listed in the `.rs.info` packet as a "slot - name"
    // pair per line
    const auto kv_pair = lines->split(" - ");
    if (kv_pair.first.getAsInteger(10, slot))
      return false;
    m_kernels.push_back(RSKernelDescriptor(this, kv_pair.second, slot));
  }
  return true;
}

bool RSModuleDescriptor::ParseExportVarCount(llvm::StringRef *lines,
                                             size_t n_lines) {
  // Skip the ExportVarCount line
  ++lines;
  for (; n_lines--; ++lines)
    m_globals.push_back(RSGlobalDescriptor(this, *lines));
  return true;
}

// The .rs.info symbol in renderscript modules contains a string which needs to
// be parsed. The string is basic and is parsed on a line by line basis.
bool RSModuleDescriptor::ParseRSInfo() {
  assert(m_module);
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));
  const Symbol *info_sym = m_module->FindFirstSymbolWithNameAndType(
      ConstString(".rs.info"), eSymbolTypeData);
  if (!info_sym)
    return false;

  const addr_t addr = info_sym->GetAddressRef().GetFileAddress();
  if (addr == LLDB_INVALID_ADDRESS)
    return false;

  const addr_t size = info_sym->GetByteSize();
  const FileSpec fs = m_module->GetFileSpec();

  auto buffer =
      FileSystem::Instance().CreateDataBuffer(fs.GetPath(), size, addr);
  if (!buffer)
    return false;

  // split rs.info. contents into lines
  llvm::SmallVector<llvm::StringRef, 128> info_lines;
  {
    const llvm::StringRef raw_rs_info((const char *)buffer->GetBytes());
    raw_rs_info.split(info_lines, '\n');
    if (log)
      log->Printf("'.rs.info symbol for '%s':\n%s",
                  m_module->GetFileSpec().GetCString(),
                  raw_rs_info.str().c_str());
  }

  enum {
    eExportVar,
    eExportForEach,
    eExportReduce,
    ePragma,
    eBuildChecksum,
    eObjectSlot,
    eVersionInfo,
  };

  const auto rs_info_handler = [](llvm::StringRef name) -> int {
    return llvm::StringSwitch<int>(name)
        // The number of visible global variables in the script
        .Case("exportVarCount", eExportVar)
        // The number of RenderScrip `forEach` kernels __attribute__((kernel))
        .Case("exportForEachCount", eExportForEach)
        // The number of generalreductions: This marked in the script by
        // `#pragma reduce()`
        .Case("exportReduceCount", eExportReduce)
        // Total count of all RenderScript specific `#pragmas` used in the
        // script
        .Case("pragmaCount", ePragma)
        .Case("objectSlotCount", eObjectSlot)
        .Case("versionInfo", eVersionInfo)
        .Default(-1);
  };

  // parse all text lines of .rs.info
  for (auto line = info_lines.begin(); line != info_lines.end(); ++line) {
    const auto kv_pair = line->split(": ");
    const auto key = kv_pair.first;
    const auto val = kv_pair.second.trim();

    const auto handler = rs_info_handler(key);
    if (handler == -1)
      continue;
    // getAsInteger returns `true` on an error condition - we're only
    // interested in numeric fields at the moment
    uint64_t n_lines;
    if (val.getAsInteger(10, n_lines)) {
      LLDB_LOGV(log, "Failed to parse non-numeric '.rs.info' section {0}",
                line->str());
      continue;
    }
    if (info_lines.end() - (line + 1) < (ptrdiff_t)n_lines)
      return false;

    bool success = false;
    switch (handler) {
    case eExportVar:
      success = ParseExportVarCount(line, n_lines);
      break;
    case eExportForEach:
      success = ParseExportForeachCount(line, n_lines);
      break;
    case eExportReduce:
      success = ParseExportReduceCount(line, n_lines);
      break;
    case ePragma:
      success = ParsePragmaCount(line, n_lines);
      break;
    case eVersionInfo:
      success = ParseVersionInfo(line, n_lines);
      break;
    default: {
      if (log)
        log->Printf("%s - skipping .rs.info field '%s'", __FUNCTION__,
                    line->str().c_str());
      continue;
    }
    }
    if (!success)
      return false;
    line += n_lines;
  }
  return info_lines.size() > 0;
}

void RenderScriptRuntime::DumpStatus(Stream &strm) const {
  if (m_libRS) {
    strm.Printf("Runtime Library discovered.");
    strm.EOL();
  }
  if (m_libRSDriver) {
    strm.Printf("Runtime Driver discovered.");
    strm.EOL();
  }
  if (m_libRSCpuRef) {
    strm.Printf("CPU Reference Implementation discovered.");
    strm.EOL();
  }

  if (m_runtimeHooks.size()) {
    strm.Printf("Runtime functions hooked:");
    strm.EOL();
    for (auto b : m_runtimeHooks) {
      strm.Indent(b.second->defn->name);
      strm.EOL();
    }
  } else {
    strm.Printf("Runtime is not hooked.");
    strm.EOL();
  }
}

void RenderScriptRuntime::DumpContexts(Stream &strm) const {
  strm.Printf("Inferred RenderScript Contexts:");
  strm.EOL();
  strm.IndentMore();

  std::map<addr_t, uint64_t> contextReferences;

  // Iterate over all of the currently discovered scripts. Note: We cant push
  // or pop from m_scripts inside this loop or it may invalidate script.
  for (const auto &script : m_scripts) {
    if (!script->context.isValid())
      continue;
    lldb::addr_t context = *script->context;

    if (contextReferences.find(context) != contextReferences.end()) {
      contextReferences[context]++;
    } else {
      contextReferences[context] = 1;
    }
  }

  for (const auto &cRef : contextReferences) {
    strm.Printf("Context 0x%" PRIx64 ": %" PRIu64 " script instances",
                cRef.first, cRef.second);
    strm.EOL();
  }
  strm.IndentLess();
}

void RenderScriptRuntime::DumpKernels(Stream &strm) const {
  strm.Printf("RenderScript Kernels:");
  strm.EOL();
  strm.IndentMore();
  for (const auto &module : m_rsmodules) {
    strm.Printf("Resource '%s':", module->m_resname.c_str());
    strm.EOL();
    for (const auto &kernel : module->m_kernels) {
      strm.Indent(kernel.m_name.AsCString());
      strm.EOL();
    }
  }
  strm.IndentLess();
}

RenderScriptRuntime::AllocationDetails *
RenderScriptRuntime::FindAllocByID(Stream &strm, const uint32_t alloc_id) {
  AllocationDetails *alloc = nullptr;

  // See if we can find allocation using id as an index;
  if (alloc_id <= m_allocations.size() && alloc_id != 0 &&
      m_allocations[alloc_id - 1]->id == alloc_id) {
    alloc = m_allocations[alloc_id - 1].get();
    return alloc;
  }

  // Fallback to searching
  for (const auto &a : m_allocations) {
    if (a->id == alloc_id) {
      alloc = a.get();
      break;
    }
  }

  if (alloc == nullptr) {
    strm.Printf("Error: Couldn't find allocation with id matching %" PRIu32,
                alloc_id);
    strm.EOL();
  }

  return alloc;
}

// Prints the contents of an allocation to the output stream, which may be a
// file
bool RenderScriptRuntime::DumpAllocation(Stream &strm, StackFrame *frame_ptr,
                                         const uint32_t id) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  // Check we can find the desired allocation
  AllocationDetails *alloc = FindAllocByID(strm, id);
  if (!alloc)
    return false; // FindAllocByID() will print error message for us here

  if (log)
    log->Printf("%s - found allocation 0x%" PRIx64, __FUNCTION__,
                *alloc->address.get());

  // Check we have information about the allocation, if not calculate it
  if (alloc->ShouldRefresh()) {
    if (log)
      log->Printf("%s - allocation details not calculated yet, jitting info.",
                  __FUNCTION__);

    // JIT all the allocation information
    if (!RefreshAllocation(alloc, frame_ptr)) {
      strm.Printf("Error: Couldn't JIT allocation details");
      strm.EOL();
      return false;
    }
  }

  // Establish format and size of each data element
  const uint32_t vec_size = *alloc->element.type_vec_size.get();
  const Element::DataType type = *alloc->element.type.get();

  assert(type >= Element::RS_TYPE_NONE && type <= Element::RS_TYPE_FONT &&
         "Invalid allocation type");

  lldb::Format format;
  if (type >= Element::RS_TYPE_ELEMENT)
    format = eFormatHex;
  else
    format = vec_size == 1
                 ? static_cast<lldb::Format>(
                       AllocationDetails::RSTypeToFormat[type][eFormatSingle])
                 : static_cast<lldb::Format>(
                       AllocationDetails::RSTypeToFormat[type][eFormatVector]);

  const uint32_t data_size = *alloc->element.datum_size.get();

  if (log)
    log->Printf("%s - element size %" PRIu32 " bytes, including padding",
                __FUNCTION__, data_size);

  // Allocate a buffer to copy data into
  std::shared_ptr<uint8_t> buffer = GetAllocationData(alloc, frame_ptr);
  if (!buffer) {
    strm.Printf("Error: Couldn't read allocation data");
    strm.EOL();
    return false;
  }

  // Calculate stride between rows as there may be padding at end of rows since
  // allocated memory is 16-byte aligned
  if (!alloc->stride.isValid()) {
    if (alloc->dimension.get()->dim_2 == 0) // We only have one dimension
      alloc->stride = 0;
    else if (!JITAllocationStride(alloc, frame_ptr)) {
      strm.Printf("Error: Couldn't calculate allocation row stride");
      strm.EOL();
      return false;
    }
  }
  const uint32_t stride = *alloc->stride.get();
  const uint32_t size = *alloc->size.get(); // Size of whole allocation
  const uint32_t padding =
      alloc->element.padding.isValid() ? *alloc->element.padding.get() : 0;
  if (log)
    log->Printf("%s - stride %" PRIu32 " bytes, size %" PRIu32
                " bytes, padding %" PRIu32,
                __FUNCTION__, stride, size, padding);

  // Find dimensions used to index loops, so need to be non-zero
  uint32_t dim_x = alloc->dimension.get()->dim_1;
  dim_x = dim_x == 0 ? 1 : dim_x;

  uint32_t dim_y = alloc->dimension.get()->dim_2;
  dim_y = dim_y == 0 ? 1 : dim_y;

  uint32_t dim_z = alloc->dimension.get()->dim_3;
  dim_z = dim_z == 0 ? 1 : dim_z;

  // Use data extractor to format output
  const uint32_t target_ptr_size =
      GetProcess()->GetTarget().GetArchitecture().GetAddressByteSize();
  DataExtractor alloc_data(buffer.get(), size, GetProcess()->GetByteOrder(),
                           target_ptr_size);

  uint32_t offset = 0;   // Offset in buffer to next element to be printed
  uint32_t prev_row = 0; // Offset to the start of the previous row

  // Iterate over allocation dimensions, printing results to user
  strm.Printf("Data (X, Y, Z):");
  for (uint32_t z = 0; z < dim_z; ++z) {
    for (uint32_t y = 0; y < dim_y; ++y) {
      // Use stride to index start of next row.
      if (!(y == 0 && z == 0))
        offset = prev_row + stride;
      prev_row = offset;

      // Print each element in the row individually
      for (uint32_t x = 0; x < dim_x; ++x) {
        strm.Printf("\n(%" PRIu32 ", %" PRIu32 ", %" PRIu32 ") = ", x, y, z);
        if ((type == Element::RS_TYPE_NONE) &&
            (alloc->element.children.size() > 0) &&
            (alloc->element.type_name != Element::GetFallbackStructName())) {
          // Here we are dumping an Element of struct type. This is done using
          // expression evaluation with the name of the struct type and pointer
          // to element. Don't print the name of the resulting expression,
          // since this will be '$[0-9]+'
          DumpValueObjectOptions expr_options;
          expr_options.SetHideName(true);

          // Setup expression as dereferencing a pointer cast to element
          // address.
          char expr_char_buffer[jit_max_expr_size];
          int written =
              snprintf(expr_char_buffer, jit_max_expr_size, "*(%s*) 0x%" PRIx64,
                       alloc->element.type_name.AsCString(),
                       *alloc->data_ptr.get() + offset);

          if (written < 0 || written >= jit_max_expr_size) {
            if (log)
              log->Printf("%s - error in snprintf().", __FUNCTION__);
            continue;
          }

          // Evaluate expression
          ValueObjectSP expr_result;
          GetProcess()->GetTarget().EvaluateExpression(expr_char_buffer,
                                                       frame_ptr, expr_result);

          // Print the results to our stream.
          expr_result->Dump(strm, expr_options);
        } else {
          DumpDataExtractor(alloc_data, &strm, offset, format,
                            data_size - padding, 1, 1, LLDB_INVALID_ADDRESS, 0,
                            0);
        }
        offset += data_size;
      }
    }
  }
  strm.EOL();

  return true;
}

// Function recalculates all our cached information about allocations by
// jitting the RS runtime regarding each allocation we know about. Returns true
// if all allocations could be recomputed, false otherwise.
bool RenderScriptRuntime::RecomputeAllAllocations(Stream &strm,
                                                  StackFrame *frame_ptr) {
  bool success = true;
  for (auto &alloc : m_allocations) {
    // JIT current allocation information
    if (!RefreshAllocation(alloc.get(), frame_ptr)) {
      strm.Printf("Error: Couldn't evaluate details for allocation %" PRIu32
                  "\n",
                  alloc->id);
      success = false;
    }
  }

  if (success)
    strm.Printf("All allocations successfully recomputed");
  strm.EOL();

  return success;
}

// Prints information regarding currently loaded allocations. These details are
// gathered by jitting the runtime, which has as latency. Index parameter
// specifies a single allocation ID to print, or a zero value to print them all
void RenderScriptRuntime::ListAllocations(Stream &strm, StackFrame *frame_ptr,
                                          const uint32_t index) {
  strm.Printf("RenderScript Allocations:");
  strm.EOL();
  strm.IndentMore();

  for (auto &alloc : m_allocations) {
    // index will only be zero if we want to print all allocations
    if (index != 0 && index != alloc->id)
      continue;

    // JIT current allocation information
    if (alloc->ShouldRefresh() && !RefreshAllocation(alloc.get(), frame_ptr)) {
      strm.Printf("Error: Couldn't evaluate details for allocation %" PRIu32,
                  alloc->id);
      strm.EOL();
      continue;
    }

    strm.Printf("%" PRIu32 ":", alloc->id);
    strm.EOL();
    strm.IndentMore();

    strm.Indent("Context: ");
    if (!alloc->context.isValid())
      strm.Printf("unknown\n");
    else
      strm.Printf("0x%" PRIx64 "\n", *alloc->context.get());

    strm.Indent("Address: ");
    if (!alloc->address.isValid())
      strm.Printf("unknown\n");
    else
      strm.Printf("0x%" PRIx64 "\n", *alloc->address.get());

    strm.Indent("Data pointer: ");
    if (!alloc->data_ptr.isValid())
      strm.Printf("unknown\n");
    else
      strm.Printf("0x%" PRIx64 "\n", *alloc->data_ptr.get());

    strm.Indent("Dimensions: ");
    if (!alloc->dimension.isValid())
      strm.Printf("unknown\n");
    else
      strm.Printf("(%" PRId32 ", %" PRId32 ", %" PRId32 ")\n",
                  alloc->dimension.get()->dim_1, alloc->dimension.get()->dim_2,
                  alloc->dimension.get()->dim_3);

    strm.Indent("Data Type: ");
    if (!alloc->element.type.isValid() ||
        !alloc->element.type_vec_size.isValid())
      strm.Printf("unknown\n");
    else {
      const int vector_size = *alloc->element.type_vec_size.get();
      Element::DataType type = *alloc->element.type.get();

      if (!alloc->element.type_name.IsEmpty())
        strm.Printf("%s\n", alloc->element.type_name.AsCString());
      else {
        // Enum value isn't monotonous, so doesn't always index
        // RsDataTypeToString array
        if (type >= Element::RS_TYPE_ELEMENT && type <= Element::RS_TYPE_FONT)
          type =
              static_cast<Element::DataType>((type - Element::RS_TYPE_ELEMENT) +
                                             Element::RS_TYPE_MATRIX_2X2 + 1);

        if (type >= (sizeof(AllocationDetails::RsDataTypeToString) /
                     sizeof(AllocationDetails::RsDataTypeToString[0])) ||
            vector_size > 4 || vector_size < 1)
          strm.Printf("invalid type\n");
        else
          strm.Printf(
              "%s\n",
              AllocationDetails::RsDataTypeToString[static_cast<uint32_t>(type)]
                                                   [vector_size - 1]);
      }
    }

    strm.Indent("Data Kind: ");
    if (!alloc->element.type_kind.isValid())
      strm.Printf("unknown\n");
    else {
      const Element::DataKind kind = *alloc->element.type_kind.get();
      if (kind < Element::RS_KIND_USER || kind > Element::RS_KIND_PIXEL_YUV)
        strm.Printf("invalid kind\n");
      else
        strm.Printf(
            "%s\n",
            AllocationDetails::RsDataKindToString[static_cast<uint32_t>(kind)]);
    }

    strm.EOL();
    strm.IndentLess();
  }
  strm.IndentLess();
}

// Set breakpoints on every kernel found in RS module
void RenderScriptRuntime::BreakOnModuleKernels(
    const RSModuleDescriptorSP rsmodule_sp) {
  for (const auto &kernel : rsmodule_sp->m_kernels) {
    // Don't set breakpoint on 'root' kernel
    if (strcmp(kernel.m_name.AsCString(), "root") == 0)
      continue;

    CreateKernelBreakpoint(kernel.m_name);
  }
}

// Method is internally called by the 'kernel breakpoint all' command to enable
// or disable breaking on all kernels. When do_break is true we want to enable
// this functionality. When do_break is false we want to disable it.
void RenderScriptRuntime::SetBreakAllKernels(bool do_break, TargetSP target) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_BREAKPOINTS));

  InitSearchFilter(target);

  // Set breakpoints on all the kernels
  if (do_break && !m_breakAllKernels) {
    m_breakAllKernels = true;

    for (const auto &module : m_rsmodules)
      BreakOnModuleKernels(module);

    if (log)
      log->Printf("%s(True) - breakpoints set on all currently loaded kernels.",
                  __FUNCTION__);
  } else if (!do_break &&
             m_breakAllKernels) // Breakpoints won't be set on any new kernels.
  {
    m_breakAllKernels = false;

    if (log)
      log->Printf("%s(False) - breakpoints no longer automatically set.",
                  __FUNCTION__);
  }
}

// Given the name of a kernel this function creates a breakpoint using our own
// breakpoint resolver, and returns the Breakpoint shared pointer.
BreakpointSP
RenderScriptRuntime::CreateKernelBreakpoint(const ConstString &name) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_BREAKPOINTS));

  if (!m_filtersp) {
    if (log)
      log->Printf("%s - error, no breakpoint search filter set.", __FUNCTION__);
    return nullptr;
  }

  BreakpointResolverSP resolver_sp(new RSBreakpointResolver(nullptr, name));
  Target &target = GetProcess()->GetTarget();
  BreakpointSP bp = target.CreateBreakpoint(
      m_filtersp, resolver_sp, false, false, false);

  // Give RS breakpoints a specific name, so the user can manipulate them as a
  // group.
  Status err;
  target.AddNameToBreakpoint(bp, "RenderScriptKernel", err);
  if (err.Fail() && log)
    if (log)
      log->Printf("%s - error setting break name, '%s'.", __FUNCTION__,
                  err.AsCString());

  return bp;
}

BreakpointSP
RenderScriptRuntime::CreateReductionBreakpoint(const ConstString &name,
                                               int kernel_types) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_BREAKPOINTS));

  if (!m_filtersp) {
    if (log)
      log->Printf("%s - error, no breakpoint search filter set.", __FUNCTION__);
    return nullptr;
  }

  BreakpointResolverSP resolver_sp(new RSReduceBreakpointResolver(
      nullptr, name, &m_rsmodules, kernel_types));
  Target &target = GetProcess()->GetTarget();
  BreakpointSP bp = target.CreateBreakpoint(
      m_filtersp, resolver_sp, false, false, false);

  // Give RS breakpoints a specific name, so the user can manipulate them as a
  // group.
  Status err;
  target.AddNameToBreakpoint(bp, "RenderScriptReduction", err);
  if (err.Fail() && log)
      log->Printf("%s - error setting break name, '%s'.", __FUNCTION__,
                  err.AsCString());

  return bp;
}

// Given an expression for a variable this function tries to calculate the
// variable's value. If this is possible it returns true and sets the uint64_t
// parameter to the variables unsigned value. Otherwise function returns false.
bool RenderScriptRuntime::GetFrameVarAsUnsigned(const StackFrameSP frame_sp,
                                                const char *var_name,
                                                uint64_t &val) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE));
  Status err;
  VariableSP var_sp;

  // Find variable in stack frame
  ValueObjectSP value_sp(frame_sp->GetValueForVariableExpressionPath(
      var_name, eNoDynamicValues,
      StackFrame::eExpressionPathOptionCheckPtrVsMember |
          StackFrame::eExpressionPathOptionsAllowDirectIVarAccess,
      var_sp, err));
  if (!err.Success()) {
    if (log)
      log->Printf("%s - error, couldn't find '%s' in frame", __FUNCTION__,
                  var_name);
    return false;
  }

  // Find the uint32_t value for the variable
  bool success = false;
  val = value_sp->GetValueAsUnsigned(0, &success);
  if (!success) {
    if (log)
      log->Printf("%s - error, couldn't parse '%s' as an uint32_t.",
                  __FUNCTION__, var_name);
    return false;
  }

  return true;
}

// Function attempts to find the current coordinate of a kernel invocation by
// investigating the values of frame variables in the .expand function. These
// coordinates are returned via the coord array reference parameter. Returns
// true if the coordinates could be found, and false otherwise.
bool RenderScriptRuntime::GetKernelCoordinate(RSCoordinate &coord,
                                              Thread *thread_ptr) {
  static const char *const x_expr = "rsIndex";
  static const char *const y_expr = "p->current.y";
  static const char *const z_expr = "p->current.z";

  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE));

  if (!thread_ptr) {
    if (log)
      log->Printf("%s - Error, No thread pointer", __FUNCTION__);

    return false;
  }

  // Walk the call stack looking for a function whose name has the suffix
  // '.expand' and contains the variables we're looking for.
  for (uint32_t i = 0; i < thread_ptr->GetStackFrameCount(); ++i) {
    if (!thread_ptr->SetSelectedFrameByIndex(i))
      continue;

    StackFrameSP frame_sp = thread_ptr->GetSelectedFrame();
    if (!frame_sp)
      continue;

    // Find the function name
    const SymbolContext sym_ctx =
        frame_sp->GetSymbolContext(eSymbolContextFunction);
    const ConstString func_name = sym_ctx.GetFunctionName();
    if (!func_name)
      continue;

    if (log)
      log->Printf("%s - Inspecting function '%s'", __FUNCTION__,
                  func_name.GetCString());

    // Check if function name has .expand suffix
    if (!func_name.GetStringRef().endswith(".expand"))
      continue;

    if (log)
      log->Printf("%s - Found .expand function '%s'", __FUNCTION__,
                  func_name.GetCString());

    // Get values for variables in .expand frame that tell us the current
    // kernel invocation
    uint64_t x, y, z;
    bool found = GetFrameVarAsUnsigned(frame_sp, x_expr, x) &&
                 GetFrameVarAsUnsigned(frame_sp, y_expr, y) &&
                 GetFrameVarAsUnsigned(frame_sp, z_expr, z);

    if (found) {
      // The RenderScript runtime uses uint32_t for these vars. If they're not
      // within bounds, our frame parsing is garbage
      assert(x <= UINT32_MAX && y <= UINT32_MAX && z <= UINT32_MAX);
      coord.x = (uint32_t)x;
      coord.y = (uint32_t)y;
      coord.z = (uint32_t)z;
      return true;
    }
  }
  return false;
}

// Callback when a kernel breakpoint hits and we're looking for a specific
// coordinate. Baton parameter contains a pointer to the target coordinate we
// want to break on. Function then checks the .expand frame for the current
// coordinate and breaks to user if it matches. Parameter 'break_id' is the id
// of the Breakpoint which made the callback. Parameter 'break_loc_id' is the
// id for the BreakpointLocation which was hit, a single logical breakpoint can
// have multiple addresses.
bool RenderScriptRuntime::KernelBreakpointHit(void *baton,
                                              StoppointCallbackContext *ctx,
                                              user_id_t break_id,
                                              user_id_t break_loc_id) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_BREAKPOINTS));

  assert(baton &&
         "Error: null baton in conditional kernel breakpoint callback");

  // Coordinate we want to stop on
  RSCoordinate target_coord = *static_cast<RSCoordinate *>(baton);

  if (log)
    log->Printf("%s - Break ID %" PRIu64 ", " FMT_COORD, __FUNCTION__, break_id,
                target_coord.x, target_coord.y, target_coord.z);

  // Select current thread
  ExecutionContext context(ctx->exe_ctx_ref);
  Thread *thread_ptr = context.GetThreadPtr();
  assert(thread_ptr && "Null thread pointer");

  // Find current kernel invocation from .expand frame variables
  RSCoordinate current_coord{};
  if (!GetKernelCoordinate(current_coord, thread_ptr)) {
    if (log)
      log->Printf("%s - Error, couldn't select .expand stack frame",
                  __FUNCTION__);
    return false;
  }

  if (log)
    log->Printf("%s - " FMT_COORD, __FUNCTION__, current_coord.x,
                current_coord.y, current_coord.z);

  // Check if the current kernel invocation coordinate matches our target
  // coordinate
  if (target_coord == current_coord) {
    if (log)
      log->Printf("%s, BREAKING " FMT_COORD, __FUNCTION__, current_coord.x,
                  current_coord.y, current_coord.z);

    BreakpointSP breakpoint_sp =
        context.GetTargetPtr()->GetBreakpointByID(break_id);
    assert(breakpoint_sp != nullptr &&
           "Error: Couldn't find breakpoint matching break id for callback");
    breakpoint_sp->SetEnabled(false); // Optimise since conditional breakpoint
                                      // should only be hit once.
    return true;
  }

  // No match on coordinate
  return false;
}

void RenderScriptRuntime::SetConditional(BreakpointSP bp, Stream &messages,
                                         const RSCoordinate &coord) {
  messages.Printf("Conditional kernel breakpoint on coordinate " FMT_COORD,
                  coord.x, coord.y, coord.z);
  messages.EOL();

  // Allocate memory for the baton, and copy over coordinate
  RSCoordinate *baton = new RSCoordinate(coord);

  // Create a callback that will be invoked every time the breakpoint is hit.
  // The baton object passed to the handler is the target coordinate we want to
  // break on.
  bp->SetCallback(KernelBreakpointHit, baton, true);

  // Store a shared pointer to the baton, so the memory will eventually be
  // cleaned up after destruction
  m_conditional_breaks[bp->GetID()] = std::unique_ptr<RSCoordinate>(baton);
}

// Tries to set a breakpoint on the start of a kernel, resolved using the
// kernel name. Argument 'coords', represents a three dimensional coordinate
// which can be used to specify a single kernel instance to break on. If this
// is set then we add a callback to the breakpoint.
bool RenderScriptRuntime::PlaceBreakpointOnKernel(TargetSP target,
                                                  Stream &messages,
                                                  const char *name,
                                                  const RSCoordinate *coord) {
  if (!name)
    return false;

  InitSearchFilter(target);

  ConstString kernel_name(name);
  BreakpointSP bp = CreateKernelBreakpoint(kernel_name);
  if (!bp)
    return false;

  // We have a conditional breakpoint on a specific coordinate
  if (coord)
    SetConditional(bp, messages, *coord);

  bp->GetDescription(&messages, lldb::eDescriptionLevelInitial, false);

  return true;
}

BreakpointSP
RenderScriptRuntime::CreateScriptGroupBreakpoint(const ConstString &name,
                                                 bool stop_on_all) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_BREAKPOINTS));

  if (!m_filtersp) {
    if (log)
      log->Printf("%s - error, no breakpoint search filter set.", __FUNCTION__);
    return nullptr;
  }

  BreakpointResolverSP resolver_sp(new RSScriptGroupBreakpointResolver(
      nullptr, name, m_scriptGroups, stop_on_all));
  Target &target = GetProcess()->GetTarget();
  BreakpointSP bp = target.CreateBreakpoint(
      m_filtersp, resolver_sp, false, false, false);
  // Give RS breakpoints a specific name, so the user can manipulate them as a
  // group.
  Status err;
  target.AddNameToBreakpoint(bp, name.GetCString(), err);
  if (err.Fail() && log)
    log->Printf("%s - error setting break name, '%s'.", __FUNCTION__,
                err.AsCString());
  // ask the breakpoint to resolve itself
  bp->ResolveBreakpoint();
  return bp;
}

bool RenderScriptRuntime::PlaceBreakpointOnScriptGroup(TargetSP target,
                                                       Stream &strm,
                                                       const ConstString &name,
                                                       bool multi) {
  InitSearchFilter(target);
  BreakpointSP bp = CreateScriptGroupBreakpoint(name, multi);
  if (bp)
    bp->GetDescription(&strm, lldb::eDescriptionLevelInitial, false);
  return bool(bp);
}

bool RenderScriptRuntime::PlaceBreakpointOnReduction(TargetSP target,
                                                     Stream &messages,
                                                     const char *reduce_name,
                                                     const RSCoordinate *coord,
                                                     int kernel_types) {
  if (!reduce_name)
    return false;

  InitSearchFilter(target);
  BreakpointSP bp =
      CreateReductionBreakpoint(ConstString(reduce_name), kernel_types);
  if (!bp)
    return false;

  if (coord)
    SetConditional(bp, messages, *coord);

  bp->GetDescription(&messages, lldb::eDescriptionLevelInitial, false);

  return true;
}

void RenderScriptRuntime::DumpModules(Stream &strm) const {
  strm.Printf("RenderScript Modules:");
  strm.EOL();
  strm.IndentMore();
  for (const auto &module : m_rsmodules) {
    module->Dump(strm);
  }
  strm.IndentLess();
}

RenderScriptRuntime::ScriptDetails *
RenderScriptRuntime::LookUpScript(addr_t address, bool create) {
  for (const auto &s : m_scripts) {
    if (s->script.isValid())
      if (*s->script == address)
        return s.get();
  }
  if (create) {
    std::unique_ptr<ScriptDetails> s(new ScriptDetails);
    s->script = address;
    m_scripts.push_back(std::move(s));
    return m_scripts.back().get();
  }
  return nullptr;
}

RenderScriptRuntime::AllocationDetails *
RenderScriptRuntime::LookUpAllocation(addr_t address) {
  for (const auto &a : m_allocations) {
    if (a->address.isValid())
      if (*a->address == address)
        return a.get();
  }
  return nullptr;
}

RenderScriptRuntime::AllocationDetails *
RenderScriptRuntime::CreateAllocation(addr_t address) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_LANGUAGE);

  // Remove any previous allocation which contains the same address
  auto it = m_allocations.begin();
  while (it != m_allocations.end()) {
    if (*((*it)->address) == address) {
      if (log)
        log->Printf("%s - Removing allocation id: %d, address: 0x%" PRIx64,
                    __FUNCTION__, (*it)->id, address);

      it = m_allocations.erase(it);
    } else {
      it++;
    }
  }

  std::unique_ptr<AllocationDetails> a(new AllocationDetails);
  a->address = address;
  m_allocations.push_back(std::move(a));
  return m_allocations.back().get();
}

bool RenderScriptRuntime::ResolveKernelName(lldb::addr_t kernel_addr,
                                            ConstString &name) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_SYMBOLS);

  Target &target = GetProcess()->GetTarget();
  Address resolved;
  // RenderScript module
  if (!target.GetSectionLoadList().ResolveLoadAddress(kernel_addr, resolved)) {
    if (log)
      log->Printf("%s: unable to resolve 0x%" PRIx64 " to a loaded symbol",
                  __FUNCTION__, kernel_addr);
    return false;
  }

  Symbol *sym = resolved.CalculateSymbolContextSymbol();
  if (!sym)
    return false;

  name = sym->GetName();
  assert(IsRenderScriptModule(resolved.CalculateSymbolContextModule()));
  if (log)
    log->Printf("%s: 0x%" PRIx64 " resolved to the symbol '%s'", __FUNCTION__,
                kernel_addr, name.GetCString());
  return true;
}

void RSModuleDescriptor::Dump(Stream &strm) const {
  int indent = strm.GetIndentLevel();

  strm.Indent();
  m_module->GetFileSpec().Dump(&strm);
  strm.Indent(m_module->GetNumCompileUnits() ? "Debug info loaded."
                                             : "Debug info does not exist.");
  strm.EOL();
  strm.IndentMore();

  strm.Indent();
  strm.Printf("Globals: %" PRIu64, static_cast<uint64_t>(m_globals.size()));
  strm.EOL();
  strm.IndentMore();
  for (const auto &global : m_globals) {
    global.Dump(strm);
  }
  strm.IndentLess();

  strm.Indent();
  strm.Printf("Kernels: %" PRIu64, static_cast<uint64_t>(m_kernels.size()));
  strm.EOL();
  strm.IndentMore();
  for (const auto &kernel : m_kernels) {
    kernel.Dump(strm);
  }
  strm.IndentLess();

  strm.Indent();
  strm.Printf("Pragmas: %" PRIu64, static_cast<uint64_t>(m_pragmas.size()));
  strm.EOL();
  strm.IndentMore();
  for (const auto &key_val : m_pragmas) {
    strm.Indent();
    strm.Printf("%s: %s", key_val.first.c_str(), key_val.second.c_str());
    strm.EOL();
  }
  strm.IndentLess();

  strm.Indent();
  strm.Printf("Reductions: %" PRIu64,
              static_cast<uint64_t>(m_reductions.size()));
  strm.EOL();
  strm.IndentMore();
  for (const auto &reduction : m_reductions) {
    reduction.Dump(strm);
  }

  strm.SetIndentLevel(indent);
}

void RSGlobalDescriptor::Dump(Stream &strm) const {
  strm.Indent(m_name.AsCString());
  VariableList var_list;
  m_module->m_module->FindGlobalVariables(m_name, nullptr, 1U, var_list);
  if (var_list.GetSize() == 1) {
    auto var = var_list.GetVariableAtIndex(0);
    auto type = var->GetType();
    if (type) {
      strm.Printf(" - ");
      type->DumpTypeName(&strm);
    } else {
      strm.Printf(" - Unknown Type");
    }
  } else {
    strm.Printf(" - variable identified, but not found in binary");
    const Symbol *s = m_module->m_module->FindFirstSymbolWithNameAndType(
        m_name, eSymbolTypeData);
    if (s) {
      strm.Printf(" (symbol exists) ");
    }
  }

  strm.EOL();
}

void RSKernelDescriptor::Dump(Stream &strm) const {
  strm.Indent(m_name.AsCString());
  strm.EOL();
}

void RSReductionDescriptor::Dump(lldb_private::Stream &stream) const {
  stream.Indent(m_reduce_name.AsCString());
  stream.IndentMore();
  stream.EOL();
  stream.Indent();
  stream.Printf("accumulator: %s", m_accum_name.AsCString());
  stream.EOL();
  stream.Indent();
  stream.Printf("initializer: %s", m_init_name.AsCString());
  stream.EOL();
  stream.Indent();
  stream.Printf("combiner: %s", m_comb_name.AsCString());
  stream.EOL();
  stream.Indent();
  stream.Printf("outconverter: %s", m_outc_name.AsCString());
  stream.EOL();
  // XXX This is currently unspecified by RenderScript, and unused
  // stream.Indent();
  // stream.Printf("halter: '%s'", m_init_name.AsCString());
  // stream.EOL();
  stream.IndentLess();
}

class CommandObjectRenderScriptRuntimeModuleDump : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeModuleDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript module dump",
            "Dumps renderscript specific information for all modules.",
            "renderscript module dump",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeModuleDump() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime =
        (RenderScriptRuntime *)m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript);
    runtime->DumpModules(result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectRenderScriptRuntimeModule : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeModule(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript module",
                               "Commands that deal with RenderScript modules.",
                               nullptr) {
    LoadSubCommand(
        "dump", CommandObjectSP(new CommandObjectRenderScriptRuntimeModuleDump(
                    interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeModule() override = default;
};

class CommandObjectRenderScriptRuntimeKernelList : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeKernelList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript kernel list",
            "Lists renderscript kernel names and associated script resources.",
            "renderscript kernel list",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeKernelList() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime =
        (RenderScriptRuntime *)m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript);
    runtime->DumpKernels(result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

static constexpr OptionDefinition g_renderscript_reduction_bp_set_options[] = {
    {LLDB_OPT_SET_1, false, "function-role", 't',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeOneLiner,
     "Break on a comma separated set of reduction kernel types "
     "(accumulator,outcoverter,combiner,initializer"},
    {LLDB_OPT_SET_1, false, "coordinate", 'c', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeValue,
     "Set a breakpoint on a single invocation of the kernel with specified "
     "coordinate.\n"
     "Coordinate takes the form 'x[,y][,z] where x,y,z are positive "
     "integers representing kernel dimensions. "
     "Any unset dimensions will be defaulted to zero."}};

class CommandObjectRenderScriptRuntimeReductionBreakpointSet
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeReductionBreakpointSet(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript reduction breakpoint set",
            "Set a breakpoint on named RenderScript general reductions",
            "renderscript reduction breakpoint set  <kernel_name> [-t "
            "<reduction_kernel_type,...>]",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused),
        m_options(){};

  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(),
          m_kernel_types(RSReduceBreakpointResolver::eKernelTypeAll) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *exe_ctx) override {
      Status err;
      StreamString err_str;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 't':
        if (!ParseReductionTypes(option_arg, err_str))
          err.SetErrorStringWithFormat(
              "Unable to deduce reduction types for %s: %s",
              option_arg.str().c_str(), err_str.GetData());
        break;
      case 'c': {
        auto coord = RSCoordinate{};
        if (!ParseCoordinate(option_arg, coord))
          err.SetErrorStringWithFormat("unable to parse coordinate for %s",
                                       option_arg.str().c_str());
        else {
          m_have_coord = true;
          m_coord = coord;
        }
        break;
      }
      default:
        err.SetErrorStringWithFormat("Invalid option '-%c'", short_option);
      }
      return err;
    }

    void OptionParsingStarting(ExecutionContext *exe_ctx) override {
      m_have_coord = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_renderscript_reduction_bp_set_options);
    }

    bool ParseReductionTypes(llvm::StringRef option_val,
                             StreamString &err_str) {
      m_kernel_types = RSReduceBreakpointResolver::eKernelTypeNone;
      const auto reduce_name_to_type = [](llvm::StringRef name) -> int {
        return llvm::StringSwitch<int>(name)
            .Case("accumulator", RSReduceBreakpointResolver::eKernelTypeAccum)
            .Case("initializer", RSReduceBreakpointResolver::eKernelTypeInit)
            .Case("outconverter", RSReduceBreakpointResolver::eKernelTypeOutC)
            .Case("combiner", RSReduceBreakpointResolver::eKernelTypeComb)
            .Case("all", RSReduceBreakpointResolver::eKernelTypeAll)
            // Currently not exposed by the runtime
            // .Case("halter", RSReduceBreakpointResolver::eKernelTypeHalter)
            .Default(0);
      };

      // Matching a comma separated list of known words is fairly
      // straightforward with PCRE, but we're using ERE, so we end up with a
      // little ugliness...
      RegularExpression::Match match(/* max_matches */ 5);
      RegularExpression match_type_list(
          llvm::StringRef("^([[:alpha:]]+)(,[[:alpha:]]+){0,4}$"));

      assert(match_type_list.IsValid());

      if (!match_type_list.Execute(option_val, &match)) {
        err_str.PutCString(
            "a comma-separated list of kernel types is required");
        return false;
      }

      // splitting on commas is much easier with llvm::StringRef than regex
      llvm::SmallVector<llvm::StringRef, 5> type_names;
      llvm::StringRef(option_val).split(type_names, ',');

      for (const auto &name : type_names) {
        const int type = reduce_name_to_type(name);
        if (!type) {
          err_str.Printf("unknown kernel type name %s", name.str().c_str());
          return false;
        }
        m_kernel_types |= type;
      }

      return true;
    }

    int m_kernel_types;
    llvm::StringRef m_reduce_name;
    RSCoordinate m_coord;
    bool m_have_coord;
  };

  Options *GetOptions() override { return &m_options; }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc < 1) {
      result.AppendErrorWithFormat("'%s' takes 1 argument of reduction name, "
                                   "and an optional kernel type list",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    auto &outstream = result.GetOutputStream();
    auto name = command.GetArgumentAtIndex(0);
    auto &target = m_exe_ctx.GetTargetSP();
    auto coord = m_options.m_have_coord ? &m_options.m_coord : nullptr;
    if (!runtime->PlaceBreakpointOnReduction(target, outstream, name, coord,
                                             m_options.m_kernel_types)) {
      result.SetStatus(eReturnStatusFailed);
      result.AppendError("Error: unable to place breakpoint on reduction");
      return false;
    }
    result.AppendMessage("Breakpoint(s) created");
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }

private:
  CommandOptions m_options;
};

static constexpr OptionDefinition g_renderscript_kernel_bp_set_options[] = {
    {LLDB_OPT_SET_1, false, "coordinate", 'c', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeValue,
     "Set a breakpoint on a single invocation of the kernel with specified "
     "coordinate.\n"
     "Coordinate takes the form 'x[,y][,z] where x,y,z are positive "
     "integers representing kernel dimensions. "
     "Any unset dimensions will be defaulted to zero."}};

class CommandObjectRenderScriptRuntimeKernelBreakpointSet
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeKernelBreakpointSet(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript kernel breakpoint set",
            "Sets a breakpoint on a renderscript kernel.",
            "renderscript kernel breakpoint set <kernel_name> [-c x,y,z]",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectRenderScriptRuntimeKernelBreakpointSet() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *exe_ctx) override {
      Status err;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c': {
        auto coord = RSCoordinate{};
        if (!ParseCoordinate(option_arg, coord))
          err.SetErrorStringWithFormat(
              "Couldn't parse coordinate '%s', should be in format 'x,y,z'.",
              option_arg.str().c_str());
        else {
          m_have_coord = true;
          m_coord = coord;
        }
        break;
      }
      default:
        err.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
        break;
      }
      return err;
    }

    void OptionParsingStarting(ExecutionContext *exe_ctx) override {
      m_have_coord = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_renderscript_kernel_bp_set_options);
    }

    RSCoordinate m_coord;
    bool m_have_coord;
  };

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc < 1) {
      result.AppendErrorWithFormat(
          "'%s' takes 1 argument of kernel name, and an optional coordinate.",
          m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime =
        (RenderScriptRuntime *)m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript);

    auto &outstream = result.GetOutputStream();
    auto &target = m_exe_ctx.GetTargetSP();
    auto name = command.GetArgumentAtIndex(0);
    auto coord = m_options.m_have_coord ? &m_options.m_coord : nullptr;
    if (!runtime->PlaceBreakpointOnKernel(target, outstream, name, coord)) {
      result.SetStatus(eReturnStatusFailed);
      result.AppendErrorWithFormat(
          "Error: unable to set breakpoint on kernel '%s'", name);
      return false;
    }

    result.AppendMessage("Breakpoint(s) created");
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }

private:
  CommandOptions m_options;
};

class CommandObjectRenderScriptRuntimeKernelBreakpointAll
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeKernelBreakpointAll(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript kernel breakpoint all",
            "Automatically sets a breakpoint on all renderscript kernels that "
            "are or will be loaded.\n"
            "Disabling option means breakpoints will no longer be set on any "
            "kernels loaded in the future, "
            "but does not remove currently set breakpoints.",
            "renderscript kernel breakpoint all <enable/disable>",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused) {}

  ~CommandObjectRenderScriptRuntimeKernelBreakpointAll() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes 1 argument of 'enable' or 'disable'", m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    bool do_break = false;
    const char *argument = command.GetArgumentAtIndex(0);
    if (strcmp(argument, "enable") == 0) {
      do_break = true;
      result.AppendMessage("Breakpoints will be set on all kernels.");
    } else if (strcmp(argument, "disable") == 0) {
      do_break = false;
      result.AppendMessage("Breakpoints will not be set on any new kernels.");
    } else {
      result.AppendErrorWithFormat(
          "Argument must be either 'enable' or 'disable'");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    runtime->SetBreakAllKernels(do_break, m_exe_ctx.GetTargetSP());

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectRenderScriptRuntimeReductionBreakpoint
    : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeReductionBreakpoint(
      CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript reduction breakpoint",
                               "Commands that manipulate breakpoints on "
                               "renderscript general reductions.",
                               nullptr) {
    LoadSubCommand(
        "set", CommandObjectSP(
                   new CommandObjectRenderScriptRuntimeReductionBreakpointSet(
                       interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeReductionBreakpoint() override = default;
};

class CommandObjectRenderScriptRuntimeKernelCoordinate
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeKernelCoordinate(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript kernel coordinate",
            "Shows the (x,y,z) coordinate of the current kernel invocation.",
            "renderscript kernel coordinate",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused) {}

  ~CommandObjectRenderScriptRuntimeKernelCoordinate() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RSCoordinate coord{};
    bool success = RenderScriptRuntime::GetKernelCoordinate(
        coord, m_exe_ctx.GetThreadPtr());
    Stream &stream = result.GetOutputStream();

    if (success) {
      stream.Printf("Coordinate: " FMT_COORD, coord.x, coord.y, coord.z);
      stream.EOL();
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      stream.Printf("Error: Coordinate could not be found.");
      stream.EOL();
      result.SetStatus(eReturnStatusFailed);
    }
    return true;
  }
};

class CommandObjectRenderScriptRuntimeKernelBreakpoint
    : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeKernelBreakpoint(
      CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "renderscript kernel",
            "Commands that generate breakpoints on renderscript kernels.",
            nullptr) {
    LoadSubCommand(
        "set",
        CommandObjectSP(new CommandObjectRenderScriptRuntimeKernelBreakpointSet(
            interpreter)));
    LoadSubCommand(
        "all",
        CommandObjectSP(new CommandObjectRenderScriptRuntimeKernelBreakpointAll(
            interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeKernelBreakpoint() override = default;
};

class CommandObjectRenderScriptRuntimeKernel : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeKernel(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript kernel",
                               "Commands that deal with RenderScript kernels.",
                               nullptr) {
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectRenderScriptRuntimeKernelList(
                    interpreter)));
    LoadSubCommand(
        "coordinate",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeKernelCoordinate(interpreter)));
    LoadSubCommand(
        "breakpoint",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeKernelBreakpoint(interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeKernel() override = default;
};

class CommandObjectRenderScriptRuntimeContextDump : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeContextDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript context dump",
                            "Dumps renderscript context information.",
                            "renderscript context dump",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeContextDump() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime =
        (RenderScriptRuntime *)m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript);
    runtime->DumpContexts(result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

static constexpr OptionDefinition g_renderscript_runtime_alloc_dump_options[] = {
    {LLDB_OPT_SET_1, false, "file", 'f', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeFilename,
     "Print results to specified file instead of command line."}};

class CommandObjectRenderScriptRuntimeContext : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeContext(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript context",
                               "Commands that deal with RenderScript contexts.",
                               nullptr) {
    LoadSubCommand(
        "dump", CommandObjectSP(new CommandObjectRenderScriptRuntimeContextDump(
                    interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeContext() override = default;
};

class CommandObjectRenderScriptRuntimeAllocationDump
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeAllocationDump(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript allocation dump",
                            "Displays the contents of a particular allocation",
                            "renderscript allocation dump <ID>",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched),
        m_options() {}

  ~CommandObjectRenderScriptRuntimeAllocationDump() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *exe_ctx) override {
      Status err;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f':
        m_outfile.SetFile(option_arg, FileSpec::Style::native);
        FileSystem::Instance().Resolve(m_outfile);
        if (FileSystem::Instance().Exists(m_outfile)) {
          m_outfile.Clear();
          err.SetErrorStringWithFormat("file already exists: '%s'",
                                       option_arg.str().c_str());
        }
        break;
      default:
        err.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
        break;
      }
      return err;
    }

    void OptionParsingStarting(ExecutionContext *exe_ctx) override {
      m_outfile.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_renderscript_runtime_alloc_dump_options);
    }

    FileSpec m_outfile;
  };

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc < 1) {
      result.AppendErrorWithFormat("'%s' takes 1 argument, an allocation ID. "
                                   "As well as an optional -f argument",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    const char *id_cstr = command.GetArgumentAtIndex(0);
    bool success = false;
    const uint32_t id =
        StringConvert::ToUInt32(id_cstr, UINT32_MAX, 0, &success);
    if (!success) {
      result.AppendErrorWithFormat("invalid allocation id argument '%s'",
                                   id_cstr);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Stream *output_strm = nullptr;
    StreamFile outfile_stream;
    const FileSpec &outfile_spec =
        m_options.m_outfile; // Dump allocation to file instead
    if (outfile_spec) {
      // Open output file
      std::string path = outfile_spec.GetPath();
      auto error = FileSystem::Instance().Open(
          outfile_stream.GetFile(), outfile_spec,
          File::eOpenOptionWrite | File::eOpenOptionCanCreate);
      if (error.Success()) {
        output_strm = &outfile_stream;
        result.GetOutputStream().Printf("Results written to '%s'",
                                        path.c_str());
        result.GetOutputStream().EOL();
      } else {
        result.AppendErrorWithFormat("Couldn't open file '%s'", path.c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else
      output_strm = &result.GetOutputStream();

    assert(output_strm != nullptr);
    bool dumped =
        runtime->DumpAllocation(*output_strm, m_exe_ctx.GetFramePtr(), id);

    if (dumped)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else
      result.SetStatus(eReturnStatusFailed);

    return true;
  }

private:
  CommandOptions m_options;
};

static constexpr OptionDefinition g_renderscript_runtime_alloc_list_options[] = {
    {LLDB_OPT_SET_1, false, "id", 'i', OptionParser::eRequiredArgument, nullptr,
     {}, 0, eArgTypeIndex,
     "Only show details of a single allocation with specified id."}};

class CommandObjectRenderScriptRuntimeAllocationList
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeAllocationList(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript allocation list",
            "List renderscript allocations and their information.",
            "renderscript allocation list",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched),
        m_options() {}

  ~CommandObjectRenderScriptRuntimeAllocationList() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options(), m_id(0) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *exe_ctx) override {
      Status err;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'i':
        if (option_arg.getAsInteger(0, m_id))
          err.SetErrorStringWithFormat("invalid integer value for option '%c'",
                                       short_option);
        break;
      default:
        err.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
        break;
      }
      return err;
    }

    void OptionParsingStarting(ExecutionContext *exe_ctx) override { m_id = 0; }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_renderscript_runtime_alloc_list_options);
    }

    uint32_t m_id;
  };

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));
    runtime->ListAllocations(result.GetOutputStream(), m_exe_ctx.GetFramePtr(),
                             m_options.m_id);
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }

private:
  CommandOptions m_options;
};

class CommandObjectRenderScriptRuntimeAllocationLoad
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeAllocationLoad(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript allocation load",
            "Loads renderscript allocation contents from a file.",
            "renderscript allocation load <ID> <filename>",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeAllocationLoad() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc != 2) {
      result.AppendErrorWithFormat(
          "'%s' takes 2 arguments, an allocation ID and filename to read from.",
          m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    const char *id_cstr = command.GetArgumentAtIndex(0);
    bool success = false;
    const uint32_t id =
        StringConvert::ToUInt32(id_cstr, UINT32_MAX, 0, &success);
    if (!success) {
      result.AppendErrorWithFormat("invalid allocation id argument '%s'",
                                   id_cstr);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *path = command.GetArgumentAtIndex(1);
    bool loaded = runtime->LoadAllocation(result.GetOutputStream(), id, path,
                                          m_exe_ctx.GetFramePtr());

    if (loaded)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else
      result.SetStatus(eReturnStatusFailed);

    return true;
  }
};

class CommandObjectRenderScriptRuntimeAllocationSave
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeAllocationSave(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript allocation save",
                            "Write renderscript allocation contents to a file.",
                            "renderscript allocation save <ID> <filename>",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeAllocationSave() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc != 2) {
      result.AppendErrorWithFormat(
          "'%s' takes 2 arguments, an allocation ID and filename to read from.",
          m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    const char *id_cstr = command.GetArgumentAtIndex(0);
    bool success = false;
    const uint32_t id =
        StringConvert::ToUInt32(id_cstr, UINT32_MAX, 0, &success);
    if (!success) {
      result.AppendErrorWithFormat("invalid allocation id argument '%s'",
                                   id_cstr);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *path = command.GetArgumentAtIndex(1);
    bool saved = runtime->SaveAllocation(result.GetOutputStream(), id, path,
                                         m_exe_ctx.GetFramePtr());

    if (saved)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else
      result.SetStatus(eReturnStatusFailed);

    return true;
  }
};

class CommandObjectRenderScriptRuntimeAllocationRefresh
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeAllocationRefresh(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript allocation refresh",
                            "Recomputes the details of all allocations.",
                            "renderscript allocation refresh",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeAllocationRefresh() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));

    bool success = runtime->RecomputeAllAllocations(result.GetOutputStream(),
                                                    m_exe_ctx.GetFramePtr());

    if (success) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return true;
    } else {
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }
};

class CommandObjectRenderScriptRuntimeAllocation
    : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeAllocation(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "renderscript allocation",
            "Commands that deal with RenderScript allocations.", nullptr) {
    LoadSubCommand(
        "list",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeAllocationList(interpreter)));
    LoadSubCommand(
        "dump",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeAllocationDump(interpreter)));
    LoadSubCommand(
        "save",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeAllocationSave(interpreter)));
    LoadSubCommand(
        "load",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeAllocationLoad(interpreter)));
    LoadSubCommand(
        "refresh",
        CommandObjectSP(new CommandObjectRenderScriptRuntimeAllocationRefresh(
            interpreter)));
  }

  ~CommandObjectRenderScriptRuntimeAllocation() override = default;
};

class CommandObjectRenderScriptRuntimeStatus : public CommandObjectParsed {
public:
  CommandObjectRenderScriptRuntimeStatus(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript status",
                            "Displays current RenderScript runtime status.",
                            "renderscript status",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptRuntimeStatus() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    RenderScriptRuntime *runtime =
        (RenderScriptRuntime *)m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript);
    runtime->DumpStatus(result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectRenderScriptRuntimeReduction
    : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntimeReduction(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript reduction",
                               "Commands that handle general reduction kernels",
                               nullptr) {
    LoadSubCommand(
        "breakpoint",
        CommandObjectSP(new CommandObjectRenderScriptRuntimeReductionBreakpoint(
            interpreter)));
  }
  ~CommandObjectRenderScriptRuntimeReduction() override = default;
};

class CommandObjectRenderScriptRuntime : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptRuntime(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "renderscript",
            "Commands for operating on the RenderScript runtime.",
            "renderscript <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "module", CommandObjectSP(
                      new CommandObjectRenderScriptRuntimeModule(interpreter)));
    LoadSubCommand(
        "status", CommandObjectSP(
                      new CommandObjectRenderScriptRuntimeStatus(interpreter)));
    LoadSubCommand(
        "kernel", CommandObjectSP(
                      new CommandObjectRenderScriptRuntimeKernel(interpreter)));
    LoadSubCommand("context",
                   CommandObjectSP(new CommandObjectRenderScriptRuntimeContext(
                       interpreter)));
    LoadSubCommand(
        "allocation",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeAllocation(interpreter)));
    LoadSubCommand("scriptgroup",
                   NewCommandObjectRenderScriptScriptGroup(interpreter));
    LoadSubCommand(
        "reduction",
        CommandObjectSP(
            new CommandObjectRenderScriptRuntimeReduction(interpreter)));
  }

  ~CommandObjectRenderScriptRuntime() override = default;
};

void RenderScriptRuntime::Initiate() { assert(!m_initiated); }

RenderScriptRuntime::RenderScriptRuntime(Process *process)
    : lldb_private::CPPLanguageRuntime(process), m_initiated(false),
      m_debuggerPresentFlagged(false), m_breakAllKernels(false),
      m_ir_passes(nullptr) {
  ModulesDidLoad(process->GetTarget().GetImages());
}

lldb::CommandObjectSP RenderScriptRuntime::GetCommandObject(
    lldb_private::CommandInterpreter &interpreter) {
  return CommandObjectSP(new CommandObjectRenderScriptRuntime(interpreter));
}

RenderScriptRuntime::~RenderScriptRuntime() = default;
