//===-- NSDictionary.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <mutex>

#include "clang/AST/DeclCXX.h"

#include "NSDictionary.h"

#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntime.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

NSDictionary_Additionals::AdditionalFormatterMatching::Prefix::Prefix(
    ConstString p)
    : m_prefix(p) {}

bool NSDictionary_Additionals::AdditionalFormatterMatching::Prefix::Match(
    ConstString class_name) {
  return class_name.GetStringRef().startswith(m_prefix.GetStringRef());
}

NSDictionary_Additionals::AdditionalFormatterMatching::Full::Full(ConstString n)
    : m_name(n) {}

bool NSDictionary_Additionals::AdditionalFormatterMatching::Full::Match(
    ConstString class_name) {
  return (class_name == m_name);
}

NSDictionary_Additionals::AdditionalFormatters<
    CXXFunctionSummaryFormat::Callback> &
NSDictionary_Additionals::GetAdditionalSummaries() {
  static AdditionalFormatters<CXXFunctionSummaryFormat::Callback> g_map;
  return g_map;
}

NSDictionary_Additionals::AdditionalFormatters<
    CXXSyntheticChildren::CreateFrontEndCallback> &
NSDictionary_Additionals::GetAdditionalSynthetics() {
  static AdditionalFormatters<CXXSyntheticChildren::CreateFrontEndCallback>
      g_map;
  return g_map;
}

static CompilerType GetLLDBNSPairType(TargetSP target_sp) {
  CompilerType compiler_type;

  ClangASTContext *target_ast_context = target_sp->GetScratchClangASTContext();

  if (target_ast_context) {
    ConstString g___lldb_autogen_nspair("__lldb_autogen_nspair");

    compiler_type =
        target_ast_context->GetTypeForIdentifier<clang::CXXRecordDecl>(
            g___lldb_autogen_nspair);

    if (!compiler_type) {
      compiler_type = target_ast_context->CreateRecordType(
          nullptr, lldb::eAccessPublic, g___lldb_autogen_nspair.GetCString(),
          clang::TTK_Struct, lldb::eLanguageTypeC);

      if (compiler_type) {
        ClangASTContext::StartTagDeclarationDefinition(compiler_type);
        CompilerType id_compiler_type =
            target_ast_context->GetBasicType(eBasicTypeObjCID);
        ClangASTContext::AddFieldToRecordType(
            compiler_type, "key", id_compiler_type, lldb::eAccessPublic, 0);
        ClangASTContext::AddFieldToRecordType(
            compiler_type, "value", id_compiler_type, lldb::eAccessPublic, 0);
        ClangASTContext::CompleteTagDeclarationDefinition(compiler_type);
      }
    }
  }
  return compiler_type;
}

namespace lldb_private {
namespace formatters {
class NSDictionaryISyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSDictionaryISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSDictionaryISyntheticFrontEnd() override;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  struct DataDescriptor_32 {
    uint32_t _used : 26;
    uint32_t _szidx : 6;
  };

  struct DataDescriptor_64 {
    uint64_t _used : 58;
    uint32_t _szidx : 6;
  };

  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size;
  lldb::ByteOrder m_order;
  DataDescriptor_32 *m_data_32;
  DataDescriptor_64 *m_data_64;
  lldb::addr_t m_data_ptr;
  CompilerType m_pair_type;
  std::vector<DictionaryItemDescriptor> m_children;
};

class NSDictionary1SyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSDictionary1SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSDictionary1SyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  ValueObjectSP m_pair;
};

template <typename D32, typename D64>
class GenericNSDictionaryMSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  GenericNSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~GenericNSDictionaryMSyntheticFrontEnd() override;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size;
  lldb::ByteOrder m_order;
  D32 *m_data_32;
  D64 *m_data_64;
  CompilerType m_pair_type;
  std::vector<DictionaryItemDescriptor> m_children;
};
  
namespace Foundation1100 {
  class NSDictionaryMSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
  public:
    NSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);
    
    ~NSDictionaryMSyntheticFrontEnd() override;
    
    size_t CalculateNumChildren() override;
    
    lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;
    
    bool Update() override;
    
    bool MightHaveChildren() override;
    
    size_t GetIndexOfChildWithName(const ConstString &name) override;
    
  private:
    struct DataDescriptor_32 {
      uint32_t _used : 26;
      uint32_t _kvo : 1;
      uint32_t _size;
      uint32_t _mutations;
      uint32_t _objs_addr;
      uint32_t _keys_addr;
    };
    
    struct DataDescriptor_64 {
      uint64_t _used : 58;
      uint32_t _kvo : 1;
      uint64_t _size;
      uint64_t _mutations;
      uint64_t _objs_addr;
      uint64_t _keys_addr;
    };
    
    struct DictionaryItemDescriptor {
      lldb::addr_t key_ptr;
      lldb::addr_t val_ptr;
      lldb::ValueObjectSP valobj_sp;
    };
    
    ExecutionContextRef m_exe_ctx_ref;
    uint8_t m_ptr_size;
    lldb::ByteOrder m_order;
    DataDescriptor_32 *m_data_32;
    DataDescriptor_64 *m_data_64;
    CompilerType m_pair_type;
    std::vector<DictionaryItemDescriptor> m_children;
  };
}
  
namespace Foundation1428 {
  struct DataDescriptor_32 {
    uint32_t _used : 26;
    uint32_t _kvo : 1;
    uint32_t _size;
    uint32_t _buffer;
    uint64_t GetSize() { return _size; }
  };
  
  struct DataDescriptor_64 {
    uint64_t _used : 58;
    uint32_t _kvo : 1;
    uint64_t _size;
    uint64_t _buffer;
    uint64_t GetSize() { return _size; }
  };
  
  
  
  using NSDictionaryMSyntheticFrontEnd =
    GenericNSDictionaryMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
}
  
namespace Foundation1437 {
  static const uint64_t NSDictionaryCapacities[] = {
      0, 3, 7, 13, 23, 41, 71, 127, 191, 251, 383, 631, 1087, 1723,
      2803, 4523, 7351, 11959, 19447, 31231, 50683, 81919, 132607,
      214519, 346607, 561109, 907759, 1468927, 2376191, 3845119,
      6221311, 10066421, 16287743, 26354171, 42641881, 68996069,
      111638519, 180634607, 292272623, 472907251
  };
  
  static const size_t NSDictionaryNumSizeBuckets = sizeof(NSDictionaryCapacities) / sizeof(uint64_t);
  
  struct DataDescriptor_32 {
    uint32_t _buffer;
    union {
      struct {
        uint32_t _mutations;
      };
      struct {
        uint32_t _muts;
        uint32_t _used:25;
        uint32_t _kvo:1;
        uint32_t _szidx:6;
      };
    };
    
    uint64_t GetSize() {
      return (_szidx) >= NSDictionaryNumSizeBuckets ?
          0 : NSDictionaryCapacities[_szidx];
    }
  };
  
  struct DataDescriptor_64 {
    uint64_t _buffer;
    union {
      struct {
        uint64_t _mutations;
      };
      struct {
        uint32_t _muts;
        uint32_t _used:25;
        uint32_t _kvo:1;
        uint32_t _szidx:6;
      };
    };
    
    uint64_t GetSize() {
      return (_szidx) >= NSDictionaryNumSizeBuckets ?
          0 : NSDictionaryCapacities[_szidx];
    }
  };
  
  using NSDictionaryMSyntheticFrontEnd =
    GenericNSDictionaryMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
  
  template <typename DD>
  uint64_t
  __NSDictionaryMSize_Impl(lldb_private::Process &process,
                           lldb::addr_t valobj_addr, Status &error) {
    const lldb::addr_t start_of_descriptor =
        valobj_addr + process.GetAddressByteSize();
    DD descriptor = DD();
    process.ReadMemory(start_of_descriptor, &descriptor, sizeof(descriptor),
                       error);
    if (error.Fail()) {
      return 0;
    }
    return descriptor._used;
  }
  
  uint64_t
  __NSDictionaryMSize(lldb_private::Process &process, lldb::addr_t valobj_addr,
               Status &error) {
    if (process.GetAddressByteSize() == 4) {
      return __NSDictionaryMSize_Impl<DataDescriptor_32>(process, valobj_addr,
                                                         error);
    } else {
      return __NSDictionaryMSize_Impl<DataDescriptor_64>(process, valobj_addr,
                                                         error);
    }
  }

}
} // namespace formatters
} // namespace lldb_private

template <bool name_entries>
bool lldb_private::formatters::NSDictionarySummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  static ConstString g_TypeHint("NSDictionary");
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();
  bool is_64bit = (ptr_size == 8);

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  uint64_t value = 0;

  ConstString class_name(descriptor->GetClassName());

  static const ConstString g_DictionaryI("__NSDictionaryI");
  static const ConstString g_DictionaryM("__NSDictionaryM");
  static const ConstString g_DictionaryMLegacy("__NSDictionaryM_Legacy");
  static const ConstString g_DictionaryMImmutable("__NSDictionaryM_Immutable");
  static const ConstString g_Dictionary1("__NSSingleEntryDictionaryI");
  static const ConstString g_Dictionary0("__NSDictionary0");
  static const ConstString g_DictionaryCF("__NSCFDictionary");

  if (class_name.IsEmpty())
    return false;

  if (class_name == g_DictionaryI || class_name == g_DictionaryMImmutable) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;
    value &= (is_64bit ? ~0xFC00000000000000UL : ~0xFC000000U);
  } else if (class_name == g_DictionaryM || class_name == g_DictionaryMLegacy ||
             class_name == g_DictionaryCF) {
    AppleObjCRuntime *apple_runtime =
    llvm::dyn_cast_or_null<AppleObjCRuntime>(runtime);
    Status error;
    if (apple_runtime && apple_runtime->GetFoundationVersion() >= 1437) {
      value = Foundation1437::__NSDictionaryMSize(*process_sp, valobj_addr,
                                                  error);
    } else {
      value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                        ptr_size, 0, error);
      value &= (is_64bit ? ~0xFC00000000000000UL : ~0xFC000000U);
    }
    if (error.Fail())
      return false;
  } else if (class_name == g_Dictionary1) {
    value = 1;
  } else if (class_name == g_Dictionary0) {
    value = 0;
  }
  else {
    auto &map(NSDictionary_Additionals::GetAdditionalSummaries());
    for (auto &candidate : map) {
      if (candidate.first && candidate.first->Match(class_name))
        return candidate.second(valobj, stream, options);
    }
    return false;
  }

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(options.GetLanguage())) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%" PRIu64 " %s%s%s", prefix.c_str(), value, "key/value pair",
                value == 1 ? "" : "s", suffix.c_str());
  return true;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::NSDictionarySyntheticFrontEndCreator(
    CXXSyntheticChildren *synth, lldb::ValueObjectSP valobj_sp) {
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return nullptr;
  AppleObjCRuntime *runtime =
      llvm::dyn_cast_or_null<AppleObjCRuntime>(process_sp->GetObjCLanguageRuntime());
  if (!runtime)
    return nullptr;

  CompilerType valobj_type(valobj_sp->GetCompilerType());
  Flags flags(valobj_type.GetTypeInfo());

  if (flags.IsClear(eTypeIsPointer)) {
    Status error;
    valobj_sp = valobj_sp->AddressOf(error);
    if (error.Fail() || !valobj_sp)
      return nullptr;
  }

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(*valobj_sp));

  if (!descriptor || !descriptor->IsValid())
    return nullptr;

  ConstString class_name(descriptor->GetClassName());

  static const ConstString g_DictionaryI("__NSDictionaryI");
  static const ConstString g_DictionaryM("__NSDictionaryM");
  static const ConstString g_Dictionary1("__NSSingleEntryDictionaryI");
  static const ConstString g_DictionaryImmutable("__NSDictionaryM_Immutable");
  static const ConstString g_DictionaryMLegacy("__NSDictionaryM_Legacy");
  static const ConstString g_Dictionary0("__NSDictionary0");

  if (class_name.IsEmpty())
    return nullptr;

  if (class_name == g_DictionaryI) {
    return (new NSDictionaryISyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_DictionaryM) {
    if (runtime->GetFoundationVersion() >= 1437) {
      return (new Foundation1437::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    } else if (runtime->GetFoundationVersion() >= 1428) {
      return (new Foundation1428::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    } else {
      return (new Foundation1100::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    }
  } else if (class_name == g_DictionaryMLegacy) {
      return (new Foundation1100::NSDictionaryMSyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_Dictionary1) {
    return (new NSDictionary1SyntheticFrontEnd(valobj_sp));
  } else {
    auto &map(NSDictionary_Additionals::GetAdditionalSynthetics());
    for (auto &candidate : map) {
      if (candidate.first && candidate.first->Match((class_name)))
        return candidate.second(synth, valobj_sp);
    }
  }

  return nullptr;
}

lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    NSDictionaryISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_ptr_size(8),
      m_order(lldb::eByteOrderInvalid), m_data_32(nullptr), m_data_64(nullptr),
      m_pair_type() {}

lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    ~NSDictionaryISyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

size_t lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}

size_t lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? m_data_32->_used : m_data_64->_used);
}

bool lldb_private::formatters::NSDictionaryISyntheticFrontEnd::Update() {
  m_children.clear();
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  m_ptr_size = 0;
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return false;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new DataDescriptor_32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(DataDescriptor_32),
                           error);
  } else {
    m_data_64 = new DataDescriptor_64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(DataDescriptor_64),
                           error);
  }
  if (error.Fail())
    return false;
  m_data_ptr = data_location + m_ptr_size;
  return false;
}

bool lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSDictionaryISyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  uint32_t num_children = CalculateNumChildren();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_data_ptr + (2 * test_idx * m_ptr_size);
      val_at_idx = key_at_idx + m_ptr_size;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    DataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    NSDictionary1SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp.get()), m_pair(nullptr) {}

size_t lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  static const ConstString g_zero("[0]");
  return name == g_zero ? 0 : UINT32_MAX;
}

size_t lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    CalculateNumChildren() {
  return 1;
}

bool lldb_private::formatters::NSDictionary1SyntheticFrontEnd::Update() {
  m_pair.reset();
  return false;
}

bool lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSDictionary1SyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  if (idx != 0)
    return lldb::ValueObjectSP();

  if (m_pair.get())
    return m_pair;

  auto process_sp(m_backend.GetProcessSP());
  if (!process_sp)
    return nullptr;

  auto ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t key_ptr =
      m_backend.GetValueAsUnsigned(LLDB_INVALID_ADDRESS) + ptr_size;
  lldb::addr_t value_ptr = key_ptr + ptr_size;

  Status error;

  lldb::addr_t value_at_idx = process_sp->ReadPointerFromMemory(key_ptr, error);
  if (error.Fail())
    return nullptr;
  lldb::addr_t key_at_idx = process_sp->ReadPointerFromMemory(value_ptr, error);
  if (error.Fail())
    return nullptr;

  auto pair_type =
      GetLLDBNSPairType(process_sp->GetTarget().shared_from_this());

  DataBufferSP buffer_sp(new DataBufferHeap(2 * ptr_size, 0));

  if (ptr_size == 8) {
    uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
    *data_ptr = key_at_idx;
    *(data_ptr + 1) = value_at_idx;
  } else {
    uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
    *data_ptr = key_at_idx;
    *(data_ptr + 1) = value_at_idx;
  }

  DataExtractor data(buffer_sp, process_sp->GetByteOrder(), ptr_size);
  m_pair = CreateValueObjectFromData(
      "[0]", data, m_backend.GetExecutionContextRef(), pair_type);

  return m_pair;
}

template <typename D32, typename D64>
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
    GenericNSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_ptr_size(8),
      m_order(lldb::eByteOrderInvalid), m_data_32(nullptr), m_data_64(nullptr),
      m_pair_type() {}

template <typename D32, typename D64>
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
    ~GenericNSDictionaryMSyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

template <typename D32, typename D64>
size_t
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::    GetIndexOfChildWithName(const ConstString &name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}

template <typename D32, typename D64>
size_t
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? m_data_32->_used : m_data_64->_used);
}

template <typename D32, typename D64>
bool
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
  Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  m_ptr_size = 0;
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return false;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new D32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(D32),
                           error);
  } else {
    m_data_64 = new D64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(D64),
                           error);
  }
  if (error.Fail())
    return false;
  return false;
}

template <typename D32, typename D64>
bool
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
    MightHaveChildren() {
  return true;
}

template <typename D32, typename D64>
lldb::ValueObjectSP
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
    GetChildAtIndex(
    size_t idx) {
  lldb::addr_t m_keys_ptr;
  lldb::addr_t m_values_ptr;
  if (m_data_32) {
    uint32_t size = m_data_32->GetSize();
    m_keys_ptr = m_data_32->_buffer;
    m_values_ptr = m_data_32->_buffer + (m_ptr_size * size);
  } else {
    uint32_t size = m_data_64->GetSize();
    m_keys_ptr = m_data_64->_buffer;
    m_values_ptr = m_data_64->_buffer + (m_ptr_size * size);
  }

  uint32_t num_children = CalculateNumChildren();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_keys_ptr + (test_idx * m_ptr_size);
      val_at_idx = m_values_ptr + (test_idx * m_ptr_size);
      ;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    DataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}


lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::
    NSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_ptr_size(8),
      m_order(lldb::eByteOrderInvalid), m_data_32(nullptr), m_data_64(nullptr),
      m_pair_type() {}

lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::~NSDictionaryMSyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

size_t
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::GetIndexOfChildWithName(const ConstString &name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}

size_t
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? m_data_32->_used : m_data_64->_used);
}

bool
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  m_ptr_size = 0;
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return false;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new DataDescriptor_32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(DataDescriptor_32),
                           error);
  } else {
    m_data_64 = new DataDescriptor_64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(DataDescriptor_64),
                           error);
  }
  if (error.Fail())
    return false;
  return false;
}

bool
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  lldb::addr_t m_keys_ptr =
      (m_data_32 ? m_data_32->_keys_addr : m_data_64->_keys_addr);
  lldb::addr_t m_values_ptr =
      (m_data_32 ? m_data_32->_objs_addr : m_data_64->_objs_addr);

  uint32_t num_children = CalculateNumChildren();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_keys_ptr + (test_idx * m_ptr_size);
      val_at_idx = m_values_ptr + (test_idx * m_ptr_size);
      ;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    DataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

template bool lldb_private::formatters::NSDictionarySummaryProvider<true>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

template bool lldb_private::formatters::NSDictionarySummaryProvider<false>(
    ValueObject &, Stream &, const TypeSummaryOptions &);
