//===-- RenderScriptRuntime.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RenderScriptRuntime_h_
#define liblldb_RenderScriptRuntime_h_

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "lldb/Core/Module.h"
#include "lldb/Expression/LLVMUserExpression.h"
#include "lldb/Target/CPPLanguageRuntime.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

namespace lldb_private {
namespace lldb_renderscript {

typedef uint32_t RSSlot;
class RSModuleDescriptor;
struct RSGlobalDescriptor;
struct RSKernelDescriptor;
struct RSReductionDescriptor;
struct RSScriptGroupDescriptor;

typedef std::shared_ptr<RSModuleDescriptor> RSModuleDescriptorSP;
typedef std::shared_ptr<RSGlobalDescriptor> RSGlobalDescriptorSP;
typedef std::shared_ptr<RSKernelDescriptor> RSKernelDescriptorSP;
typedef std::shared_ptr<RSScriptGroupDescriptor> RSScriptGroupDescriptorSP;

struct RSCoordinate {
  uint32_t x, y, z;

  RSCoordinate() : x(), y(), z(){};

  bool operator==(const lldb_renderscript::RSCoordinate &rhs) {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }
};

// Breakpoint Resolvers decide where a breakpoint is placed, so having our own
// allows us to limit the search scope to RS kernel modules. As well as check
// for .expand kernels as a fallback.
class RSBreakpointResolver : public BreakpointResolver {
public:
  RSBreakpointResolver(Breakpoint *bp, ConstString name)
      : BreakpointResolver(bp, BreakpointResolver::NameResolver),
        m_kernel_name(name) {}

  void GetDescription(Stream *strm) override {
    if (strm)
      strm->Printf("RenderScript kernel breakpoint for '%s'",
                   m_kernel_name.AsCString());
  }

  void Dump(Stream *s) const override {}

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override { return lldb::eSearchDepthModule; }

  lldb::BreakpointResolverSP
  CopyForBreakpoint(Breakpoint &breakpoint) override {
    lldb::BreakpointResolverSP ret_sp(
        new RSBreakpointResolver(&breakpoint, m_kernel_name));
    return ret_sp;
  }

protected:
  ConstString m_kernel_name;
};

class RSReduceBreakpointResolver : public BreakpointResolver {
public:
  enum ReduceKernelTypeFlags {
    eKernelTypeAll = ~(0),
    eKernelTypeNone = 0,
    eKernelTypeAccum = (1 << 0),
    eKernelTypeInit = (1 << 1),
    eKernelTypeComb = (1 << 2),
    eKernelTypeOutC = (1 << 3),
    eKernelTypeHalter = (1 << 4)
  };

  RSReduceBreakpointResolver(
      Breakpoint *breakpoint, ConstString reduce_name,
      std::vector<lldb_renderscript::RSModuleDescriptorSP> *rs_modules,
      int kernel_types = eKernelTypeAll)
      : BreakpointResolver(breakpoint, BreakpointResolver::NameResolver),
        m_reduce_name(reduce_name), m_rsmodules(rs_modules),
        m_kernel_types(kernel_types) {
    // The reduce breakpoint resolver handles adding breakpoints for named
    // reductions.
    // Breakpoints will be resolved for all constituent kernels in the named
    // reduction
  }

  void GetDescription(Stream *strm) override {
    if (strm)
      strm->Printf("RenderScript reduce breakpoint for '%s'",
                   m_reduce_name.AsCString());
  }

  void Dump(Stream *s) const override {}

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override { return lldb::eSearchDepthModule; }

  lldb::BreakpointResolverSP
  CopyForBreakpoint(Breakpoint &breakpoint) override {
    lldb::BreakpointResolverSP ret_sp(new RSReduceBreakpointResolver(
        &breakpoint, m_reduce_name, m_rsmodules, m_kernel_types));
    return ret_sp;
  }

private:
  ConstString m_reduce_name; // The name of the reduction
  std::vector<lldb_renderscript::RSModuleDescriptorSP> *m_rsmodules;
  int m_kernel_types;
};

struct RSKernelDescriptor {
public:
  RSKernelDescriptor(const RSModuleDescriptor *module, llvm::StringRef name,
                     uint32_t slot)
      : m_module(module), m_name(name), m_slot(slot) {}

  void Dump(Stream &strm) const;

  const RSModuleDescriptor *m_module;
  ConstString m_name;
  RSSlot m_slot;
};

struct RSGlobalDescriptor {
public:
  RSGlobalDescriptor(const RSModuleDescriptor *module, llvm::StringRef name)
      : m_module(module), m_name(name) {}

  void Dump(Stream &strm) const;

  const RSModuleDescriptor *m_module;
  ConstString m_name;
};

struct RSReductionDescriptor {
  RSReductionDescriptor(const RSModuleDescriptor *module, uint32_t sig,
                        uint32_t accum_data_size, llvm::StringRef name,
                        llvm::StringRef init_name, llvm::StringRef accum_name,
                        llvm::StringRef comb_name, llvm::StringRef outc_name,
                        llvm::StringRef halter_name = ".")
      : m_module(module), m_reduce_name(name), m_init_name(init_name),
        m_accum_name(accum_name), m_comb_name(comb_name),
        m_outc_name(outc_name), m_halter_name(halter_name) {
    // TODO Check whether the combiner is an autogenerated name, and track
    // this
  }

  void Dump(Stream &strm) const;

  const RSModuleDescriptor *m_module;
  ConstString m_reduce_name; // This is the name given to the general reduction
                             // as a group as passed to pragma
  // reduce(m_reduce_name). There is no kernel function with this name
  ConstString m_init_name;  // The name of the initializer name. "." if no
                            // initializer given
  ConstString m_accum_name; // The accumulator function name. "." if not given
  ConstString m_comb_name; // The name of the combiner function. If this was not
                           // given, a name is generated by the
                           // compiler. TODO
  ConstString m_outc_name; // The name of the outconverter

  ConstString m_halter_name; // The name of the halter function. XXX This is not
                             // yet specified by the RenderScript
  // compiler or runtime, and its semantics and existence is still under
  // discussion by the
  // RenderScript Contributors
  RSSlot m_accum_sig; // metatdata signature for this reduction (bitwise mask of
                      // type information (see
                      // libbcc/include/bcinfo/MetadataExtractor.h
  uint32_t m_accum_data_size; // Data size of the accumulator function input
  bool m_comb_name_generated; // Was the combiner name generated by the compiler
};

class RSModuleDescriptor {
  std::string m_slang_version;
  std::string m_bcc_version;

  bool ParseVersionInfo(llvm::StringRef *, size_t n_lines);

  bool ParseExportForeachCount(llvm::StringRef *, size_t n_lines);

  bool ParseExportVarCount(llvm::StringRef *, size_t n_lines);

  bool ParseExportReduceCount(llvm::StringRef *, size_t n_lines);

  bool ParseBuildChecksum(llvm::StringRef *, size_t n_lines);

  bool ParsePragmaCount(llvm::StringRef *, size_t n_lines);

public:
  RSModuleDescriptor(const lldb::ModuleSP &module) : m_module(module) {}

  ~RSModuleDescriptor() = default;

  bool ParseRSInfo();

  void Dump(Stream &strm) const;

  void WarnIfVersionMismatch(Stream *s) const;

  const lldb::ModuleSP m_module;
  std::vector<RSKernelDescriptor> m_kernels;
  std::vector<RSGlobalDescriptor> m_globals;
  std::vector<RSReductionDescriptor> m_reductions;
  std::map<std::string, std::string> m_pragmas;
  std::string m_resname;
};

struct RSScriptGroupDescriptor {
  struct Kernel {
    ConstString m_name;
    lldb::addr_t m_addr;
  };
  ConstString m_name;
  std::vector<Kernel> m_kernels;
};

typedef std::vector<RSScriptGroupDescriptorSP> RSScriptGroupList;

class RSScriptGroupBreakpointResolver : public BreakpointResolver {
public:
  RSScriptGroupBreakpointResolver(Breakpoint *bp, const ConstString &name,
                                  const RSScriptGroupList &groups,
                                  bool stop_on_all)
      : BreakpointResolver(bp, BreakpointResolver::NameResolver),
        m_group_name(name), m_script_groups(groups),
        m_stop_on_all(stop_on_all) {}

  void GetDescription(Stream *strm) override {
    if (strm)
      strm->Printf("RenderScript ScriptGroup breakpoint for '%s'",
                   m_group_name.AsCString());
  }

  void Dump(Stream *s) const override {}

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override { return lldb::eSearchDepthModule; }

  lldb::BreakpointResolverSP
  CopyForBreakpoint(Breakpoint &breakpoint) override {
    lldb::BreakpointResolverSP ret_sp(new RSScriptGroupBreakpointResolver(
        &breakpoint, m_group_name, m_script_groups, m_stop_on_all));
    return ret_sp;
  }

protected:
  const RSScriptGroupDescriptorSP
  FindScriptGroup(const ConstString &name) const {
    for (auto sg : m_script_groups) {
      if (ConstString::Compare(sg->m_name, name) == 0)
        return sg;
    }
    return RSScriptGroupDescriptorSP();
  }

  ConstString m_group_name;
  const RSScriptGroupList &m_script_groups;
  bool m_stop_on_all;
};
} // namespace lldb_renderscript

class RenderScriptRuntime : public lldb_private::CPPLanguageRuntime {
public:
  enum ModuleKind {
    eModuleKindIgnored,
    eModuleKindLibRS,
    eModuleKindDriver,
    eModuleKindImpl,
    eModuleKindKernelObj
  };

  ~RenderScriptRuntime() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static lldb::CommandObjectSP
  GetCommandObject(CommandInterpreter &interpreter);

  static lldb_private::ConstString GetPluginNameStatic();

  static bool IsRenderScriptModule(const lldb::ModuleSP &module_sp);

  static ModuleKind GetModuleKind(const lldb::ModuleSP &module_sp);

  static void ModulesDidLoad(const lldb::ProcessSP &process_sp,
                             const ModuleList &module_list);

  bool IsVTableName(const char *name) override;

  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  TypeAndOrName FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                 ValueObject &static_value) override;

  bool CouldHaveDynamicValue(ValueObject &in_value) override;

  lldb::BreakpointResolverSP CreateExceptionResolver(Breakpoint *bp,
                                                     bool catch_bp,
                                                     bool throw_bp) override;

  bool LoadModule(const lldb::ModuleSP &module_sp);

  void DumpModules(Stream &strm) const;

  void DumpContexts(Stream &strm) const;

  void DumpKernels(Stream &strm) const;

  bool DumpAllocation(Stream &strm, StackFrame *frame_ptr, const uint32_t id);

  void ListAllocations(Stream &strm, StackFrame *frame_ptr,
                       const uint32_t index);

  bool RecomputeAllAllocations(Stream &strm, StackFrame *frame_ptr);

  bool PlaceBreakpointOnKernel(
      lldb::TargetSP target, Stream &messages, const char *name,
      const lldb_renderscript::RSCoordinate *coords = nullptr);

  bool PlaceBreakpointOnReduction(
      lldb::TargetSP target, Stream &messages, const char *reduce_name,
      const lldb_renderscript::RSCoordinate *coords = nullptr,
      int kernel_types = ~(0));

  bool PlaceBreakpointOnScriptGroup(lldb::TargetSP target, Stream &strm,
                                    const ConstString &name, bool stop_on_all);

  void SetBreakAllKernels(bool do_break, lldb::TargetSP target);

  void DumpStatus(Stream &strm) const;

  void ModulesDidLoad(const ModuleList &module_list) override;

  bool LoadAllocation(Stream &strm, const uint32_t alloc_id,
                      const char *filename, StackFrame *frame_ptr);

  bool SaveAllocation(Stream &strm, const uint32_t alloc_id,
                      const char *filename, StackFrame *frame_ptr);

  void Update();

  void Initiate();

  const lldb_renderscript::RSScriptGroupList &GetScriptGroups() const {
    return m_scriptGroups;
  };

  bool IsKnownKernel(const ConstString &name) {
    for (const auto &module : m_rsmodules)
      for (const auto &kernel : module->m_kernels)
        if (kernel.m_name == name)
          return true;
    return false;
  }

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  static bool GetKernelCoordinate(lldb_renderscript::RSCoordinate &coord,
                                  Thread *thread_ptr);

  bool ResolveKernelName(lldb::addr_t kernel_address, ConstString &name);

protected:
  struct ScriptDetails;
  struct AllocationDetails;
  struct Element;

  lldb_renderscript::RSScriptGroupList m_scriptGroups;

  void InitSearchFilter(lldb::TargetSP target) {
    if (!m_filtersp)
      m_filtersp.reset(new SearchFilterForUnconstrainedSearches(target));
  }

  void FixupScriptDetails(lldb_renderscript::RSModuleDescriptorSP rsmodule_sp);

  void LoadRuntimeHooks(lldb::ModuleSP module, ModuleKind kind);

  bool RefreshAllocation(AllocationDetails *alloc, StackFrame *frame_ptr);

  bool EvalRSExpression(const char *expression, StackFrame *frame_ptr,
                        uint64_t *result);

  lldb::BreakpointSP CreateScriptGroupBreakpoint(const ConstString &name,
                                                 bool multi);

  lldb::BreakpointSP CreateKernelBreakpoint(const ConstString &name);

  lldb::BreakpointSP CreateReductionBreakpoint(const ConstString &name,
                                               int kernel_types);

  void BreakOnModuleKernels(
      const lldb_renderscript::RSModuleDescriptorSP rsmodule_sp);

  struct RuntimeHook;
  typedef void (RenderScriptRuntime::*CaptureStateFn)(
      RuntimeHook *hook_info,
      ExecutionContext &context); // Please do this!

  struct HookDefn {
    const char *name;
    const char *symbol_name_m32; // mangled name for the 32 bit architectures
    const char *symbol_name_m64; // mangled name for the 64 bit archs
    uint32_t version;
    ModuleKind kind;
    CaptureStateFn grabber;
  };

  struct RuntimeHook {
    lldb::addr_t address;
    const HookDefn *defn;
    lldb::BreakpointSP bp_sp;
  };

  typedef std::shared_ptr<RuntimeHook> RuntimeHookSP;

  lldb::ModuleSP m_libRS;
  lldb::ModuleSP m_libRSDriver;
  lldb::ModuleSP m_libRSCpuRef;
  std::vector<lldb_renderscript::RSModuleDescriptorSP> m_rsmodules;

  std::vector<std::unique_ptr<ScriptDetails>> m_scripts;
  std::vector<std::unique_ptr<AllocationDetails>> m_allocations;

  std::map<lldb::addr_t, lldb_renderscript::RSModuleDescriptorSP>
      m_scriptMappings;
  std::map<lldb::addr_t, RuntimeHookSP> m_runtimeHooks;
  std::map<lldb::user_id_t, std::unique_ptr<lldb_renderscript::RSCoordinate>>
      m_conditional_breaks;

  lldb::SearchFilterSP
      m_filtersp; // Needed to create breakpoints through Target API

  bool m_initiated;
  bool m_debuggerPresentFlagged;
  bool m_breakAllKernels;
  static const HookDefn s_runtimeHookDefns[];
  static const size_t s_runtimeHookCount;
  LLVMUserExpression::IRPasses *m_ir_passes;

private:
  RenderScriptRuntime(Process *process); // Call CreateInstance instead.

  static bool HookCallback(void *baton, StoppointCallbackContext *ctx,
                           lldb::user_id_t break_id,
                           lldb::user_id_t break_loc_id);

  static bool KernelBreakpointHit(void *baton, StoppointCallbackContext *ctx,
                                  lldb::user_id_t break_id,
                                  lldb::user_id_t break_loc_id);

  void HookCallback(RuntimeHook *hook_info, ExecutionContext &context);

  // Callback function when 'debugHintScriptGroup2' executes on the target.
  void CaptureDebugHintScriptGroup2(RuntimeHook *hook_info,
                                    ExecutionContext &context);

  void CaptureScriptInit(RuntimeHook *hook_info, ExecutionContext &context);

  void CaptureAllocationInit(RuntimeHook *hook_info, ExecutionContext &context);

  void CaptureAllocationDestroy(RuntimeHook *hook_info,
                                ExecutionContext &context);

  void CaptureSetGlobalVar(RuntimeHook *hook_info, ExecutionContext &context);

  void CaptureScriptInvokeForEachMulti(RuntimeHook *hook_info,
                                       ExecutionContext &context);

  AllocationDetails *FindAllocByID(Stream &strm, const uint32_t alloc_id);

  std::shared_ptr<uint8_t> GetAllocationData(AllocationDetails *alloc,
                                             StackFrame *frame_ptr);

  void SetElementSize(Element &elem);

  static bool GetFrameVarAsUnsigned(const lldb::StackFrameSP,
                                    const char *var_name, uint64_t &val);

  void FindStructTypeName(Element &elem, StackFrame *frame_ptr);

  size_t PopulateElementHeaders(const std::shared_ptr<uint8_t> header_buffer,
                                size_t offset, const Element &elem);

  size_t CalculateElementHeaderSize(const Element &elem);

  void SetConditional(lldb::BreakpointSP bp, lldb_private::Stream &messages,
                      const lldb_renderscript::RSCoordinate &coord);
  //
  // Helper functions for jitting the runtime
  //

  bool JITDataPointer(AllocationDetails *alloc, StackFrame *frame_ptr,
                      uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);

  bool JITTypePointer(AllocationDetails *alloc, StackFrame *frame_ptr);

  bool JITTypePacked(AllocationDetails *alloc, StackFrame *frame_ptr);

  bool JITElementPacked(Element &elem, const lldb::addr_t context,
                        StackFrame *frame_ptr);

  bool JITAllocationSize(AllocationDetails *alloc, StackFrame *frame_ptr);

  bool JITSubelements(Element &elem, const lldb::addr_t context,
                      StackFrame *frame_ptr);

  bool JITAllocationStride(AllocationDetails *alloc, StackFrame *frame_ptr);

  // Search for a script detail object using a target address.
  // If a script does not currently exist this function will return nullptr.
  // If 'create' is true and there is no previous script with this address,
  // then a new Script detail object will be created for this address and
  // returned.
  ScriptDetails *LookUpScript(lldb::addr_t address, bool create);

  // Search for a previously saved allocation detail object using a target
  // address.
  // If an allocation does not exist for this address then nullptr will be
  // returned.
  AllocationDetails *LookUpAllocation(lldb::addr_t address);

  // Creates a new allocation with the specified address assigning a new ID and
  // removes
  // any previous stored allocation which has the same address.
  AllocationDetails *CreateAllocation(lldb::addr_t address);

  bool GetOverrideExprOptions(clang::TargetOptions &prototype) override;

  bool GetIRPasses(LLVMUserExpression::IRPasses &passes) override;
};

} // namespace lldb_private

#endif // liblldb_RenderScriptRuntime_h_
