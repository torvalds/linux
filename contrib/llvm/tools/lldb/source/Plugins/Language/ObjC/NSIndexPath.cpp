//===-- NSIndexPath.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Cocoa.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static constexpr size_t PACKED_INDEX_SHIFT_64(size_t i) {
  return (60 - (13 * (4 - i)));
}

static constexpr size_t PACKED_INDEX_SHIFT_32(size_t i) {
  return (32 - (13 * (2 - i)));
}

class NSIndexPathSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSIndexPathSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp.get()), m_descriptor_sp(nullptr),
        m_impl(), m_ptr_size(0), m_uint_star_type() {
    m_ptr_size =
        m_backend.GetTargetSP()->GetArchitecture().GetAddressByteSize();
  }

  ~NSIndexPathSyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override { return m_impl.GetNumIndexes(); }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    return m_impl.GetIndexAtIndex(idx, m_uint_star_type);
  }

  bool Update() override {
    m_impl.Clear();

    TypeSystem *type_system = m_backend.GetCompilerType().GetTypeSystem();
    if (!type_system)
      return false;

    ClangASTContext *ast = m_backend.GetExecutionContextRef()
                               .GetTargetSP()
                               ->GetScratchClangASTContext();
    if (!ast)
      return false;

    m_uint_star_type = ast->GetPointerSizedIntType(false);

    static ConstString g__indexes("_indexes");
    static ConstString g__length("_length");

    ProcessSP process_sp = m_backend.GetProcessSP();
    if (!process_sp)
      return false;

    ObjCLanguageRuntime *runtime =
        (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
            lldb::eLanguageTypeObjC);

    if (!runtime)
      return false;

    ObjCLanguageRuntime::ClassDescriptorSP descriptor(
        runtime->GetClassDescriptor(m_backend));

    if (!descriptor.get() || !descriptor->IsValid())
      return false;

    uint64_t info_bits(0), value_bits(0), payload(0);

    if (descriptor->GetTaggedPointerInfo(&info_bits, &value_bits, &payload)) {
      m_impl.m_inlined.SetIndexes(payload, *process_sp);
      m_impl.m_mode = Mode::Inlined;
    } else {
      ObjCLanguageRuntime::ClassDescriptor::iVarDescriptor _indexes_id;
      ObjCLanguageRuntime::ClassDescriptor::iVarDescriptor _length_id;

      bool has_indexes(false), has_length(false);

      for (size_t x = 0; x < descriptor->GetNumIVars(); x++) {
        const auto &ivar = descriptor->GetIVarAtIndex(x);
        if (ivar.m_name == g__indexes) {
          _indexes_id = ivar;
          has_indexes = true;
        } else if (ivar.m_name == g__length) {
          _length_id = ivar;
          has_length = true;
        }

        if (has_length && has_indexes)
          break;
      }

      if (has_length && has_indexes) {
        m_impl.m_outsourced.m_indexes =
            m_backend
                .GetSyntheticChildAtOffset(_indexes_id.m_offset,
                                           m_uint_star_type.GetPointerType(),
                                           true)
                .get();
        ValueObjectSP length_sp(m_backend.GetSyntheticChildAtOffset(
            _length_id.m_offset, m_uint_star_type, true));
        if (length_sp) {
          m_impl.m_outsourced.m_count = length_sp->GetValueAsUnsigned(0);
          if (m_impl.m_outsourced.m_indexes)
            m_impl.m_mode = Mode::Outsourced;
        }
      }
    }
    return false;
  }

  bool MightHaveChildren() override { return m_impl.m_mode != Mode::Invalid; }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    const char *item_name = name.GetCString();
    uint32_t idx = ExtractIndexFromString(item_name);
    if (idx < UINT32_MAX && idx >= CalculateNumChildren())
      return UINT32_MAX;
    return idx;
  }

  lldb::ValueObjectSP GetSyntheticValue() override { return nullptr; }

protected:
  ObjCLanguageRuntime::ClassDescriptorSP m_descriptor_sp;

  enum class Mode { Inlined, Outsourced, Invalid };

  struct Impl {
    size_t GetNumIndexes() {
      switch (m_mode) {
      case Mode::Inlined:
        return m_inlined.GetNumIndexes();
      case Mode::Outsourced:
        return m_outsourced.m_count;
      default:
        return 0;
      }
    }

    lldb::ValueObjectSP GetIndexAtIndex(size_t idx,
                                        const CompilerType &desired_type) {
      if (idx >= GetNumIndexes())
        return nullptr;
      switch (m_mode) {
      default:
        return nullptr;
      case Mode::Inlined:
        return m_inlined.GetIndexAtIndex(idx, desired_type);
      case Mode::Outsourced:
        return m_outsourced.GetIndexAtIndex(idx);
      }
    }

    struct InlinedIndexes {
    public:
      void SetIndexes(uint64_t value, Process &p) {
        m_indexes = value;
        _lengthForInlinePayload(p.GetAddressByteSize());
        m_process = &p;
      }

      size_t GetNumIndexes() { return m_count; }

      lldb::ValueObjectSP GetIndexAtIndex(size_t idx,
                                          const CompilerType &desired_type) {
        if (!m_process)
          return nullptr;

        std::pair<uint64_t, bool> value(_indexAtPositionForInlinePayload(idx));
        if (!value.second)
          return nullptr;

        Value v;
        if (m_ptr_size == 8) {
          Scalar scalar((unsigned long long)value.first);
          v = Value(scalar);
        } else {
          Scalar scalar((unsigned int)value.first);
          v = Value(scalar);
        }

        v.SetCompilerType(desired_type);

        StreamString idx_name;
        idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);

        return ValueObjectConstResult::Create(
            m_process, v, ConstString(idx_name.GetString()));
      }

      void Clear() {
        m_indexes = 0;
        m_count = 0;
        m_ptr_size = 0;
        m_process = nullptr;
      }

      InlinedIndexes()
          : m_indexes(0), m_count(0), m_ptr_size(0), m_process(nullptr) {}

    private:
      uint64_t m_indexes;
      size_t m_count;
      uint32_t m_ptr_size;
      Process *m_process;

      // cfr. Foundation for the details of this code
      size_t _lengthForInlinePayload(uint32_t ptr_size) {
        m_ptr_size = ptr_size;
        if (m_ptr_size == 8)
          m_count = ((m_indexes >> 3) & 0x7);
        else
          m_count = ((m_indexes >> 3) & 0x3);
        return m_count;
      }

      std::pair<uint64_t, bool> _indexAtPositionForInlinePayload(size_t pos) {
        static const uint64_t PACKED_INDEX_MASK = ((1 << 13) - 1);
        if (m_ptr_size == 8) {
          switch (pos) {
          case 3:
          case 2:
          case 1:
          case 0:
            return {(m_indexes >> PACKED_INDEX_SHIFT_64(pos)) &
                        PACKED_INDEX_MASK,
                    true};
          default:
            return {0, false};
          }
        } else {
          switch (pos) {
          case 0:
          case 1:
            return {(m_indexes >> PACKED_INDEX_SHIFT_32(pos)) &
                        PACKED_INDEX_MASK,
                    true};
          default:
            return {0, false};
          }
        }
        return {0, false};
      }
    };

    struct OutsourcedIndexes {
      lldb::ValueObjectSP GetIndexAtIndex(size_t idx) {
        if (m_indexes) {
          ValueObjectSP index_sp(m_indexes->GetSyntheticArrayMember(idx, true));
          return index_sp;
        }
        return nullptr;
      }

      void Clear() {
        m_indexes = nullptr;
        m_count = 0;
      }

      OutsourcedIndexes() : m_indexes(nullptr), m_count(0) {}

      ValueObject *m_indexes;
      size_t m_count;
    };

    union {
      struct InlinedIndexes m_inlined;
      struct OutsourcedIndexes m_outsourced;
    };

    void Clear() {
      m_mode = Mode::Invalid;
      m_inlined.Clear();
      m_outsourced.Clear();
    }

    Impl() : m_mode(Mode::Invalid) {}

    Mode m_mode;
  } m_impl;

  uint32_t m_ptr_size;
  CompilerType m_uint_star_type;
};

namespace lldb_private {
namespace formatters {

SyntheticChildrenFrontEnd *
NSIndexPathSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                    lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new NSIndexPathSyntheticFrontEnd(valobj_sp);
  return nullptr;
}

} // namespace formatters
} // namespace lldb_private
