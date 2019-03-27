//===-- ValueObject.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObject_h_
#define liblldb_ValueObject_h_

#include "lldb/Core/Value.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/SharedCluster.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <stddef.h>
#include <stdint.h>
namespace lldb_private {
class Declaration;
}
namespace lldb_private {
class DumpValueObjectOptions;
}
namespace lldb_private {
class EvaluateExpressionOptions;
}
namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class Log;
}
namespace lldb_private {
class Scalar;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolContextScope;
}
namespace lldb_private {
class TypeFormatImpl;
}
namespace lldb_private {
class TypeSummaryImpl;
}
namespace lldb_private {
class TypeSummaryOptions;
}
namespace lldb_private {

/// ValueObject:
///
/// This abstract class provides an interface to a particular value, be it a
/// register, a local or global variable,
/// that is evaluated in some particular scope.  The ValueObject also has the
/// capability of being the "child" of
/// some other variable object, and in turn of having children.
/// If a ValueObject is a root variable object - having no parent - then it must
/// be constructed with respect to some
/// particular ExecutionContextScope.  If it is a child, it inherits the
/// ExecutionContextScope from its parent.
/// The ValueObject will update itself if necessary before fetching its value,
/// summary, object description, etc.
/// But it will always update itself in the ExecutionContextScope with which it
/// was originally created.

/// A brief note on life cycle management for ValueObjects.  This is a little
/// tricky because a ValueObject can contain
/// various other ValueObjects - the Dynamic Value, its children, the
/// dereference value, etc.  Any one of these can be
/// handed out as a shared pointer, but for that contained value object to be
/// valid, the root object and potentially other
/// of the value objects need to stay around.
/// We solve this problem by handing out shared pointers to the Value Object and
/// any of its dependents using a shared
/// ClusterManager.  This treats each shared pointer handed out for the entire
/// cluster as a reference to the whole
/// cluster.  The whole cluster will stay around until the last reference is
/// released.
///
/// The ValueObject mostly handle this automatically, if a value object is made
/// with a Parent ValueObject, then it adds
/// itself to the ClusterManager of the parent.

/// It does mean that external to the ValueObjects we should only ever make
/// available ValueObjectSP's, never ValueObjects
/// or pointers to them.  So all the "Root level" ValueObject derived
/// constructors should be private, and
/// should implement a Create function that new's up object and returns a Shared
/// Pointer that it gets from the GetSP() method.
///
/// However, if you are making an derived ValueObject that will be contained in
/// a parent value object, you should just
/// hold onto a pointer to it internally, and by virtue of passing the parent
/// ValueObject into its constructor, it will
/// be added to the ClusterManager for the parent.  Then if you ever hand out a
/// Shared Pointer to the contained ValueObject,
/// just do so by calling GetSP() on the contained object.

class ValueObject : public UserID {
public:
  enum GetExpressionPathFormat {
    eGetExpressionPathFormatDereferencePointers = 1,
    eGetExpressionPathFormatHonorPointers
  };

  enum ValueObjectRepresentationStyle {
    eValueObjectRepresentationStyleValue = 1,
    eValueObjectRepresentationStyleSummary,
    eValueObjectRepresentationStyleLanguageSpecific,
    eValueObjectRepresentationStyleLocation,
    eValueObjectRepresentationStyleChildrenCount,
    eValueObjectRepresentationStyleType,
    eValueObjectRepresentationStyleName,
    eValueObjectRepresentationStyleExpressionPath
  };

  enum ExpressionPathScanEndReason {
    eExpressionPathScanEndReasonEndOfString = 1,      // out of data to parse
    eExpressionPathScanEndReasonNoSuchChild,          // child element not found
    eExpressionPathScanEndReasonNoSuchSyntheticChild, // (synthetic) child
                                                      // element not found
    eExpressionPathScanEndReasonEmptyRangeNotAllowed, // [] only allowed for
                                                      // arrays
    eExpressionPathScanEndReasonDotInsteadOfArrow, // . used when -> should be
                                                   // used
    eExpressionPathScanEndReasonArrowInsteadOfDot, // -> used when . should be
                                                   // used
    eExpressionPathScanEndReasonFragileIVarNotAllowed,   // ObjC ivar expansion
                                                         // not allowed
    eExpressionPathScanEndReasonRangeOperatorNotAllowed, // [] not allowed by
                                                         // options
    eExpressionPathScanEndReasonRangeOperatorInvalid, // [] not valid on objects
                                                      // other than scalars,
                                                      // pointers or arrays
    eExpressionPathScanEndReasonArrayRangeOperatorMet, // [] is good for arrays,
                                                       // but I cannot parse it
    eExpressionPathScanEndReasonBitfieldRangeOperatorMet, // [] is good for
                                                          // bitfields, but I
                                                          // cannot parse after
                                                          // it
    eExpressionPathScanEndReasonUnexpectedSymbol, // something is malformed in
                                                  // the expression
    eExpressionPathScanEndReasonTakingAddressFailed,   // impossible to apply &
                                                       // operator
    eExpressionPathScanEndReasonDereferencingFailed,   // impossible to apply *
                                                       // operator
    eExpressionPathScanEndReasonRangeOperatorExpanded, // [] was expanded into a
                                                       // VOList
    eExpressionPathScanEndReasonSyntheticValueMissing, // getting the synthetic
                                                       // children failed
    eExpressionPathScanEndReasonUnknown = 0xFFFF
  };

  enum ExpressionPathEndResultType {
    eExpressionPathEndResultTypePlain = 1,       // anything but...
    eExpressionPathEndResultTypeBitfield,        // a bitfield
    eExpressionPathEndResultTypeBoundedRange,    // a range [low-high]
    eExpressionPathEndResultTypeUnboundedRange,  // a range []
    eExpressionPathEndResultTypeValueObjectList, // several items in a VOList
    eExpressionPathEndResultTypeInvalid = 0xFFFF
  };

  enum ExpressionPathAftermath {
    eExpressionPathAftermathNothing = 1, // just return it
    eExpressionPathAftermathDereference, // dereference the target
    eExpressionPathAftermathTakeAddress  // take target's address
  };

  enum ClearUserVisibleDataItems {
    eClearUserVisibleDataItemsNothing = 1u << 0,
    eClearUserVisibleDataItemsValue = 1u << 1,
    eClearUserVisibleDataItemsSummary = 1u << 2,
    eClearUserVisibleDataItemsLocation = 1u << 3,
    eClearUserVisibleDataItemsDescription = 1u << 4,
    eClearUserVisibleDataItemsSyntheticChildren = 1u << 5,
    eClearUserVisibleDataItemsValidator = 1u << 6,
    eClearUserVisibleDataItemsAllStrings =
        eClearUserVisibleDataItemsValue | eClearUserVisibleDataItemsSummary |
        eClearUserVisibleDataItemsLocation |
        eClearUserVisibleDataItemsDescription,
    eClearUserVisibleDataItemsAll = 0xFFFF
  };

  struct GetValueForExpressionPathOptions {
    enum class SyntheticChildrenTraversal {
      None,
      ToSynthetic,
      FromSynthetic,
      Both
    };

    bool m_check_dot_vs_arrow_syntax;
    bool m_no_fragile_ivar;
    bool m_allow_bitfields_syntax;
    SyntheticChildrenTraversal m_synthetic_children_traversal;

    GetValueForExpressionPathOptions(
        bool dot = false, bool no_ivar = false, bool bitfield = true,
        SyntheticChildrenTraversal synth_traverse =
            SyntheticChildrenTraversal::ToSynthetic)
        : m_check_dot_vs_arrow_syntax(dot), m_no_fragile_ivar(no_ivar),
          m_allow_bitfields_syntax(bitfield),
          m_synthetic_children_traversal(synth_traverse) {}

    GetValueForExpressionPathOptions &DoCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DontCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = false;
      return *this;
    }

    GetValueForExpressionPathOptions &DoAllowFragileIVar() {
      m_no_fragile_ivar = false;
      return *this;
    }

    GetValueForExpressionPathOptions &DontAllowFragileIVar() {
      m_no_fragile_ivar = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DoAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DontAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = false;
      return *this;
    }

    GetValueForExpressionPathOptions &
    SetSyntheticChildrenTraversal(SyntheticChildrenTraversal traverse) {
      m_synthetic_children_traversal = traverse;
      return *this;
    }

    static const GetValueForExpressionPathOptions DefaultOptions() {
      static GetValueForExpressionPathOptions g_default_options;

      return g_default_options;
    }
  };

  class EvaluationPoint {
  public:
    EvaluationPoint();

    EvaluationPoint(ExecutionContextScope *exe_scope,
                    bool use_selected = false);

    EvaluationPoint(const EvaluationPoint &rhs);

    ~EvaluationPoint();

    const ExecutionContextRef &GetExecutionContextRef() const {
      return m_exe_ctx_ref;
    }

    // Set the EvaluationPoint to the values in exe_scope, Return true if the
    // Evaluation Point changed. Since the ExecutionContextScope is always
    // going to be valid currently, the Updated Context will also always be
    // valid.

    //        bool
    //        SetContext (ExecutionContextScope *exe_scope);

    void SetIsConstant() {
      SetUpdated();
      m_mod_id.SetInvalid();
    }

    bool IsConstant() const { return !m_mod_id.IsValid(); }

    ProcessModID GetModID() const { return m_mod_id; }

    void SetUpdateID(ProcessModID new_id) { m_mod_id = new_id; }

    void SetNeedsUpdate() { m_needs_update = true; }

    void SetUpdated();

    bool NeedsUpdating(bool accept_invalid_exe_ctx) {
      SyncWithProcessState(accept_invalid_exe_ctx);
      return m_needs_update;
    }

    bool IsValid() {
      const bool accept_invalid_exe_ctx = false;
      if (!m_mod_id.IsValid())
        return false;
      else if (SyncWithProcessState(accept_invalid_exe_ctx)) {
        if (!m_mod_id.IsValid())
          return false;
      }
      return true;
    }

    void SetInvalid() {
      // Use the stop id to mark us as invalid, leave the thread id and the
      // stack id around for logging and history purposes.
      m_mod_id.SetInvalid();

      // Can't update an invalid state.
      m_needs_update = false;
    }

  private:
    bool SyncWithProcessState(bool accept_invalid_exe_ctx);

    ProcessModID m_mod_id; // This is the stop id when this ValueObject was last
                           // evaluated.
    ExecutionContextRef m_exe_ctx_ref;
    bool m_needs_update;
  };

  virtual ~ValueObject();

  const EvaluationPoint &GetUpdatePoint() const { return m_update_point; }

  EvaluationPoint &GetUpdatePoint() { return m_update_point; }

  const ExecutionContextRef &GetExecutionContextRef() const {
    return m_update_point.GetExecutionContextRef();
  }

  lldb::TargetSP GetTargetSP() const {
    return m_update_point.GetExecutionContextRef().GetTargetSP();
  }

  lldb::ProcessSP GetProcessSP() const {
    return m_update_point.GetExecutionContextRef().GetProcessSP();
  }

  lldb::ThreadSP GetThreadSP() const {
    return m_update_point.GetExecutionContextRef().GetThreadSP();
  }

  lldb::StackFrameSP GetFrameSP() const {
    return m_update_point.GetExecutionContextRef().GetFrameSP();
  }

  void SetNeedsUpdate();

  CompilerType GetCompilerType();

  // this vends a TypeImpl that is useful at the SB API layer
  virtual TypeImpl GetTypeImpl();

  virtual bool CanProvideValue();

  //------------------------------------------------------------------
  // Subclasses must implement the functions below.
  //------------------------------------------------------------------
  virtual uint64_t GetByteSize() = 0;

  virtual lldb::ValueType GetValueType() const = 0;

  //------------------------------------------------------------------
  // Subclasses can implement the functions below.
  //------------------------------------------------------------------
  virtual ConstString GetTypeName();

  virtual ConstString GetDisplayTypeName();

  virtual ConstString GetQualifiedTypeName();

  virtual lldb::LanguageType GetObjectRuntimeLanguage();

  virtual uint32_t
  GetTypeInfo(CompilerType *pointee_or_element_compiler_type = nullptr);

  virtual bool IsPointerType();

  virtual bool IsArrayType();

  virtual bool IsScalarType();

  virtual bool IsPointerOrReferenceType();

  virtual bool IsPossibleDynamicType();

  bool IsNilReference();

  bool IsUninitializedReference();

  virtual bool IsBaseClass() { return false; }

  bool IsBaseClass(uint32_t &depth);

  virtual bool IsDereferenceOfParent() { return false; }

  bool IsIntegerType(bool &is_signed);

  virtual bool GetBaseClassPath(Stream &s);

  virtual void GetExpressionPath(
      Stream &s, bool qualify_cxx_base_classes,
      GetExpressionPathFormat = eGetExpressionPathFormatDereferencePointers);

  lldb::ValueObjectSP GetValueForExpressionPath(
      llvm::StringRef expression,
      ExpressionPathScanEndReason *reason_to_stop = nullptr,
      ExpressionPathEndResultType *final_value_type = nullptr,
      const GetValueForExpressionPathOptions &options =
          GetValueForExpressionPathOptions::DefaultOptions(),
      ExpressionPathAftermath *final_task_on_target = nullptr);

  virtual bool IsInScope() { return true; }

  virtual lldb::offset_t GetByteOffset() { return 0; }

  virtual uint32_t GetBitfieldBitSize() { return 0; }

  virtual uint32_t GetBitfieldBitOffset() { return 0; }

  bool IsBitfield() {
    return (GetBitfieldBitSize() != 0) || (GetBitfieldBitOffset() != 0);
  }

  virtual bool IsArrayItemForPointer() { return m_is_array_item_for_pointer; }

  virtual const char *GetValueAsCString();

  virtual bool GetValueAsCString(const lldb_private::TypeFormatImpl &format,
                                 std::string &destination);

  bool GetValueAsCString(lldb::Format format, std::string &destination);

  virtual uint64_t GetValueAsUnsigned(uint64_t fail_value,
                                      bool *success = nullptr);

  virtual int64_t GetValueAsSigned(int64_t fail_value, bool *success = nullptr);

  virtual bool SetValueFromCString(const char *value_str, Status &error);

  // Return the module associated with this value object in case the value is
  // from an executable file and might have its data in sections of the file.
  // This can be used for variables.
  virtual lldb::ModuleSP GetModule();

  ValueObject *GetRoot();

  // Given a ValueObject, loop over itself and its parent, and its parent's
  // parent, .. until either the given callback returns false, or you end up at
  // a null pointer
  ValueObject *FollowParentChain(std::function<bool(ValueObject *)>);

  virtual bool GetDeclaration(Declaration &decl);

  //------------------------------------------------------------------
  // The functions below should NOT be modified by subclasses
  //------------------------------------------------------------------
  const Status &GetError();

  const ConstString &GetName() const;

  virtual lldb::ValueObjectSP GetChildAtIndex(size_t idx, bool can_create);

  // this will always create the children if necessary
  lldb::ValueObjectSP GetChildAtIndexPath(llvm::ArrayRef<size_t> idxs,
                                          size_t *index_of_error = nullptr);

  lldb::ValueObjectSP
  GetChildAtIndexPath(llvm::ArrayRef<std::pair<size_t, bool>> idxs,
                      size_t *index_of_error = nullptr);

  // this will always create the children if necessary
  lldb::ValueObjectSP GetChildAtNamePath(llvm::ArrayRef<ConstString> names,
                                         ConstString *name_of_error = nullptr);

  lldb::ValueObjectSP
  GetChildAtNamePath(llvm::ArrayRef<std::pair<ConstString, bool>> names,
                     ConstString *name_of_error = nullptr);

  virtual lldb::ValueObjectSP GetChildMemberWithName(const ConstString &name,
                                                     bool can_create);

  virtual size_t GetIndexOfChildWithName(const ConstString &name);

  size_t GetNumChildren(uint32_t max = UINT32_MAX);

  const Value &GetValue() const;

  Value &GetValue();

  virtual bool ResolveValue(Scalar &scalar);

  // return 'false' whenever you set the error, otherwise callers may assume
  // true means everything is OK - this will break breakpoint conditions among
  // potentially a few others
  virtual bool IsLogicalTrue(Status &error);

  virtual const char *GetLocationAsCString();

  const char *
  GetSummaryAsCString(lldb::LanguageType lang = lldb::eLanguageTypeUnknown);

  bool
  GetSummaryAsCString(TypeSummaryImpl *summary_ptr, std::string &destination,
                      lldb::LanguageType lang = lldb::eLanguageTypeUnknown);

  bool GetSummaryAsCString(std::string &destination,
                           const TypeSummaryOptions &options);

  bool GetSummaryAsCString(TypeSummaryImpl *summary_ptr,
                           std::string &destination,
                           const TypeSummaryOptions &options);

  std::pair<TypeValidatorResult, std::string> GetValidationStatus();

  const char *GetObjectDescription();

  bool HasSpecialPrintableRepresentation(
      ValueObjectRepresentationStyle val_obj_display,
      lldb::Format custom_format);

  enum class PrintableRepresentationSpecialCases : bool {
    eDisable = false,
    eAllow = true
  };

  bool
  DumpPrintableRepresentation(Stream &s,
                              ValueObjectRepresentationStyle val_obj_display =
                                  eValueObjectRepresentationStyleSummary,
                              lldb::Format custom_format = lldb::eFormatInvalid,
                              PrintableRepresentationSpecialCases special =
                                  PrintableRepresentationSpecialCases::eAllow,
                              bool do_dump_error = true);
  bool GetValueIsValid() const;

  // If you call this on a newly created ValueObject, it will always return
  // false.
  bool GetValueDidChange();

  bool UpdateValueIfNeeded(bool update_format = true);

  bool UpdateFormatsIfNeeded();

  lldb::ValueObjectSP GetSP() { return m_manager->GetSharedPointer(this); }

  // Change the name of the current ValueObject. Should *not* be used from a
  // synthetic child provider as it would change the name of the non synthetic
  // child as well.
  void SetName(const ConstString &name);

  virtual lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                                    AddressType *address_type = nullptr);

  lldb::addr_t GetPointerValue(AddressType *address_type = nullptr);

  lldb::ValueObjectSP GetSyntheticChild(const ConstString &key) const;

  lldb::ValueObjectSP GetSyntheticArrayMember(size_t index, bool can_create);

  lldb::ValueObjectSP GetSyntheticBitFieldChild(uint32_t from, uint32_t to,
                                                bool can_create);

  lldb::ValueObjectSP GetSyntheticExpressionPathChild(const char *expression,
                                                      bool can_create);

  virtual lldb::ValueObjectSP
  GetSyntheticChildAtOffset(uint32_t offset, const CompilerType &type,
                            bool can_create,
                            ConstString name_const_str = ConstString());

  virtual lldb::ValueObjectSP
  GetSyntheticBase(uint32_t offset, const CompilerType &type, bool can_create,
                   ConstString name_const_str = ConstString());

  virtual lldb::ValueObjectSP GetDynamicValue(lldb::DynamicValueType valueType);

  lldb::DynamicValueType GetDynamicValueType();

  virtual lldb::ValueObjectSP GetStaticValue();

  virtual lldb::ValueObjectSP GetNonSyntheticValue();

  lldb::ValueObjectSP GetSyntheticValue(bool use_synthetic = true);

  virtual bool HasSyntheticValue();

  virtual bool IsSynthetic() { return false; }

  lldb::ValueObjectSP
  GetQualifiedRepresentationIfAvailable(lldb::DynamicValueType dynValue,
                                        bool synthValue);

  virtual lldb::ValueObjectSP CreateConstantValue(const ConstString &name);

  virtual lldb::ValueObjectSP Dereference(Status &error);

  // Creates a copy of the ValueObject with a new name and setting the current
  // ValueObject as its parent. It should be used when we want to change the
  // name of a ValueObject without modifying the actual ValueObject itself
  // (e.g. sythetic child provider).
  virtual lldb::ValueObjectSP Clone(const ConstString &new_name);

  virtual lldb::ValueObjectSP AddressOf(Status &error);

  virtual lldb::addr_t GetLiveAddress() { return LLDB_INVALID_ADDRESS; }

  virtual void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                              AddressType address_type = eAddressTypeLoad) {}

  virtual lldb::ValueObjectSP Cast(const CompilerType &compiler_type);

  virtual lldb::ValueObjectSP CastPointerType(const char *name,
                                              CompilerType &ast_type);

  virtual lldb::ValueObjectSP CastPointerType(const char *name,
                                              lldb::TypeSP &type_sp);

  // The backing bits of this value object were updated, clear any descriptive
  // string, so we know we have to refetch them
  virtual void ValueUpdated() {
    ClearUserVisibleData(eClearUserVisibleDataItemsValue |
                         eClearUserVisibleDataItemsSummary |
                         eClearUserVisibleDataItemsDescription);
  }

  virtual bool IsDynamic() { return false; }

  virtual bool DoesProvideSyntheticValue() { return false; }

  virtual bool IsSyntheticChildrenGenerated();

  virtual void SetSyntheticChildrenGenerated(bool b);

  virtual SymbolContextScope *GetSymbolContextScope();

  void Dump(Stream &s);

  void Dump(Stream &s, const DumpValueObjectOptions &options);

  static lldb::ValueObjectSP
  CreateValueObjectFromExpression(llvm::StringRef name,
                                  llvm::StringRef expression,
                                  const ExecutionContext &exe_ctx);

  static lldb::ValueObjectSP
  CreateValueObjectFromExpression(llvm::StringRef name,
                                  llvm::StringRef expression,
                                  const ExecutionContext &exe_ctx,
                                  const EvaluateExpressionOptions &options);

  static lldb::ValueObjectSP
  CreateValueObjectFromAddress(llvm::StringRef name, uint64_t address,
                               const ExecutionContext &exe_ctx,
                               CompilerType type);

  static lldb::ValueObjectSP
  CreateValueObjectFromData(llvm::StringRef name, const DataExtractor &data,
                            const ExecutionContext &exe_ctx, CompilerType type);

  void LogValueObject(Log *log);

  void LogValueObject(Log *log, const DumpValueObjectOptions &options);

  lldb::ValueObjectSP Persist();

  // returns true if this is a char* or a char[] if it is a char* and
  // check_pointer is true, it also checks that the pointer is valid
  bool IsCStringContainer(bool check_pointer = false);

  std::pair<size_t, bool>
  ReadPointedString(lldb::DataBufferSP &buffer_sp, Status &error,
                    uint32_t max_length = 0, bool honor_array = true,
                    lldb::Format item_format = lldb::eFormatCharArray);

  virtual size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                                uint32_t item_count = 1);

  virtual uint64_t GetData(DataExtractor &data, Status &error);

  virtual bool SetData(DataExtractor &data, Status &error);

  virtual bool GetIsConstant() const { return m_update_point.IsConstant(); }

  bool NeedsUpdating() {
    const bool accept_invalid_exe_ctx =
        (CanUpdateWithInvalidExecutionContext() == eLazyBoolYes);
    return m_update_point.NeedsUpdating(accept_invalid_exe_ctx);
  }

  void SetIsConstant() { m_update_point.SetIsConstant(); }

  lldb::Format GetFormat() const;

  virtual void SetFormat(lldb::Format format) {
    if (format != m_format)
      ClearUserVisibleData(eClearUserVisibleDataItemsValue);
    m_format = format;
  }

  virtual lldb::LanguageType GetPreferredDisplayLanguage();

  void SetPreferredDisplayLanguage(lldb::LanguageType);

  lldb::TypeSummaryImplSP GetSummaryFormat() {
    UpdateFormatsIfNeeded();
    return m_type_summary_sp;
  }

  void SetSummaryFormat(lldb::TypeSummaryImplSP format) {
    m_type_summary_sp = format;
    ClearUserVisibleData(eClearUserVisibleDataItemsSummary);
  }

  lldb::TypeValidatorImplSP GetValidator() {
    UpdateFormatsIfNeeded();
    return m_type_validator_sp;
  }

  void SetValidator(lldb::TypeValidatorImplSP format) {
    m_type_validator_sp = format;
    ClearUserVisibleData(eClearUserVisibleDataItemsValidator);
  }

  void SetValueFormat(lldb::TypeFormatImplSP format) {
    m_type_format_sp = format;
    ClearUserVisibleData(eClearUserVisibleDataItemsValue);
  }

  lldb::TypeFormatImplSP GetValueFormat() {
    UpdateFormatsIfNeeded();
    return m_type_format_sp;
  }

  void SetSyntheticChildren(const lldb::SyntheticChildrenSP &synth_sp) {
    if (synth_sp.get() == m_synthetic_children_sp.get())
      return;
    ClearUserVisibleData(eClearUserVisibleDataItemsSyntheticChildren);
    m_synthetic_children_sp = synth_sp;
  }

  lldb::SyntheticChildrenSP GetSyntheticChildren() {
    UpdateFormatsIfNeeded();
    return m_synthetic_children_sp;
  }

  // Use GetParent for display purposes, but if you want to tell the parent to
  // update itself then use m_parent.  The ValueObjectDynamicValue's parent is
  // not the correct parent for displaying, they are really siblings, so for
  // display it needs to route through to its grandparent.
  virtual ValueObject *GetParent() { return m_parent; }

  virtual const ValueObject *GetParent() const { return m_parent; }

  ValueObject *GetNonBaseClassParent();

  void SetAddressTypeOfChildren(AddressType at) {
    m_address_type_of_ptr_or_ref_children = at;
  }

  AddressType GetAddressTypeOfChildren();

  void SetHasCompleteType() { m_did_calculate_complete_objc_class_type = true; }

  //------------------------------------------------------------------
  /// Find out if a ValueObject might have children.
  ///
  /// This call is much more efficient than CalculateNumChildren() as
  /// it doesn't need to complete the underlying type. This is designed
  /// to be used in a UI environment in order to detect if the
  /// disclosure triangle should be displayed or not.
  ///
  /// This function returns true for class, union, structure,
  /// pointers, references, arrays and more. Again, it does so without
  /// doing any expensive type completion.
  ///
  /// @return
  ///     Returns \b true if the ValueObject might have children, or \b
  ///     false otherwise.
  //------------------------------------------------------------------
  virtual bool MightHaveChildren();

  virtual lldb::VariableSP GetVariable() { return nullptr; }

  virtual bool IsRuntimeSupportValue();

  virtual uint64_t GetLanguageFlags();

  virtual void SetLanguageFlags(uint64_t flags);

protected:
  typedef ClusterManager<ValueObject> ValueObjectManager;

  class ChildrenManager {
  public:
    ChildrenManager() : m_mutex(), m_children(), m_children_count(0) {}

    bool HasChildAtIndex(size_t idx) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      return (m_children.find(idx) != m_children.end());
    }

    ValueObject *GetChildAtIndex(size_t idx) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      const auto iter = m_children.find(idx);
      return ((iter == m_children.end()) ? nullptr : iter->second);
    }

    void SetChildAtIndex(size_t idx, ValueObject *valobj) {
      // we do not need to be mutex-protected to make a pair
      ChildrenPair pair(idx, valobj);
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      m_children.insert(pair);
    }

    void SetChildrenCount(size_t count) { Clear(count); }

    size_t GetChildrenCount() { return m_children_count; }

    void Clear(size_t new_count = 0) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      m_children_count = new_count;
      m_children.clear();
    }

  private:
    typedef std::map<size_t, ValueObject *> ChildrenMap;
    typedef ChildrenMap::iterator ChildrenIterator;
    typedef ChildrenMap::value_type ChildrenPair;
    std::recursive_mutex m_mutex;
    ChildrenMap m_children;
    size_t m_children_count;
  };

  //------------------------------------------------------------------
  // Classes that inherit from ValueObject can see and modify these
  //------------------------------------------------------------------
  ValueObject
      *m_parent; // The parent value object, or nullptr if this has no parent
  ValueObject *m_root; // The root of the hierarchy for this ValueObject (or
                       // nullptr if never calculated)
  EvaluationPoint m_update_point; // Stores both the stop id and the full
                                  // context at which this value was last
  // updated.  When we are asked to update the value object, we check whether
  // the context & stop id are the same before updating.
  ConstString m_name; // The name of this object
  DataExtractor
      m_data; // A data extractor that can be used to extract the value.
  Value m_value;
  Status
      m_error; // An error object that can describe any errors that occur when
               // updating values.
  std::string m_value_str; // Cached value string that will get cleared if/when
                           // the value is updated.
  std::string m_old_value_str; // Cached old value string from the last time the
                               // value was gotten
  std::string m_location_str;  // Cached location string that will get cleared
                               // if/when the value is updated.
  std::string m_summary_str;   // Cached summary string that will get cleared
                               // if/when the value is updated.
  std::string m_object_desc_str; // Cached result of the "object printer".  This
                                 // differs from the summary
  // in that the summary is consed up by us, the object_desc_string is builtin.

  llvm::Optional<std::pair<TypeValidatorResult, std::string>>
      m_validation_result;

  CompilerType m_override_type; // If the type of the value object should be
                                // overridden, the type to impose.

  ValueObjectManager *m_manager; // This object is managed by the root object
                                 // (any ValueObject that gets created
  // without a parent.)  The manager gets passed through all the generations of
  // dependent objects, and will keep the whole cluster of objects alive as
  // long as a shared pointer to any of them has been handed out.  Shared
  // pointers to value objects must always be made with the GetSP method.

  ChildrenManager m_children;
  std::map<ConstString, ValueObject *> m_synthetic_children;

  ValueObject *m_dynamic_value;
  ValueObject *m_synthetic_value;
  ValueObject *m_deref_valobj;

  lldb::ValueObjectSP m_addr_of_valobj_sp; // We have to hold onto a shared
                                           // pointer to this one because it is
                                           // created
  // as an independent ValueObjectConstResult, which isn't managed by us.

  lldb::Format m_format;
  lldb::Format m_last_format;
  uint32_t m_last_format_mgr_revision;
  lldb::TypeSummaryImplSP m_type_summary_sp;
  lldb::TypeFormatImplSP m_type_format_sp;
  lldb::SyntheticChildrenSP m_synthetic_children_sp;
  lldb::TypeValidatorImplSP m_type_validator_sp;
  ProcessModID m_user_id_of_forced_summary;
  AddressType m_address_type_of_ptr_or_ref_children;

  llvm::SmallVector<uint8_t, 16> m_value_checksum;

  lldb::LanguageType m_preferred_display_language;

  uint64_t m_language_flags;

  bool m_value_is_valid : 1, m_value_did_change : 1, m_children_count_valid : 1,
      m_old_value_valid : 1, m_is_deref_of_parent : 1,
      m_is_array_item_for_pointer : 1, m_is_bitfield_for_scalar : 1,
      m_is_child_at_offset : 1, m_is_getting_summary : 1,
      m_did_calculate_complete_objc_class_type : 1,
      m_is_synthetic_children_generated : 1;

  friend class ValueObjectChild;
  friend class ClangExpressionDeclMap; // For GetValue
  friend class ExpressionVariable;     // For SetName
  friend class Target;                 // For SetName
  friend class ValueObjectConstResultImpl;
  friend class ValueObjectSynthetic; // For ClearUserVisibleData

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------

  // Use the no-argument constructor to make a constant variable object (with
  // no ExecutionContextScope.)

  ValueObject();

  // Use this constructor to create a "root variable object".  The ValueObject
  // will be locked to this context through-out its lifespan.

  ValueObject(ExecutionContextScope *exe_scope,
              AddressType child_ptr_or_ref_addr_type = eAddressTypeLoad);

  // Use this constructor to create a ValueObject owned by another ValueObject.
  // It will inherit the ExecutionContext of its parent.

  ValueObject(ValueObject &parent);

  ValueObjectManager *GetManager() { return m_manager; }

  virtual bool UpdateValue() = 0;

  virtual LazyBool CanUpdateWithInvalidExecutionContext() {
    return eLazyBoolCalculate;
  }

  virtual void CalculateDynamicValue(lldb::DynamicValueType use_dynamic);

  virtual lldb::DynamicValueType GetDynamicValueTypeImpl() {
    return lldb::eNoDynamicValues;
  }

  virtual bool HasDynamicValueTypeInfo() { return false; }

  virtual void CalculateSyntheticValue(bool use_synthetic = true);

  // Should only be called by ValueObject::GetChildAtIndex() Returns a
  // ValueObject managed by this ValueObject's manager.
  virtual ValueObject *CreateChildAtIndex(size_t idx,
                                          bool synthetic_array_member,
                                          int32_t synthetic_index);

  // Should only be called by ValueObject::GetNumChildren()
  virtual size_t CalculateNumChildren(uint32_t max = UINT32_MAX) = 0;

  void SetNumChildren(size_t num_children);

  void SetValueDidChange(bool value_changed);

  void SetValueIsValid(bool valid);

  void ClearUserVisibleData(
      uint32_t items = ValueObject::eClearUserVisibleDataItemsAllStrings);

  void AddSyntheticChild(const ConstString &key, ValueObject *valobj);

  DataExtractor &GetDataExtractor();

  void ClearDynamicTypeInformation();

  //------------------------------------------------------------------
  // Subclasses must implement the functions below.
  //------------------------------------------------------------------

  virtual CompilerType GetCompilerTypeImpl() = 0;

  const char *GetLocationAsCStringImpl(const Value &value,
                                       const DataExtractor &data);

  bool IsChecksumEmpty();

  void SetPreferredDisplayLanguageIfNeeded(lldb::LanguageType);

private:
  virtual CompilerType MaybeCalculateCompleteType();

  lldb::ValueObjectSP GetValueForExpressionPath_Impl(
      llvm::StringRef expression_cstr,
      ExpressionPathScanEndReason *reason_to_stop,
      ExpressionPathEndResultType *final_value_type,
      const GetValueForExpressionPathOptions &options,
      ExpressionPathAftermath *final_task_on_target);

  DISALLOW_COPY_AND_ASSIGN(ValueObject);
};

//------------------------------------------------------------------------------
// A value object manager class that is seeded with the static variable value
// and it vends the user facing value object. If the type is dynamic it can
// vend the dynamic type. If this user type also has a synthetic type
// associated with it, it will vend the synthetic type. The class watches the
// process' stop
// ID and will update the user type when needed.
//------------------------------------------------------------------------------
class ValueObjectManager {
  // The root value object is the static typed variable object.
  lldb::ValueObjectSP m_root_valobj_sp;
  // The user value object is the value object the user wants to see.
  lldb::ValueObjectSP m_user_valobj_sp;
  lldb::DynamicValueType m_use_dynamic;
  uint32_t m_stop_id; // The stop ID that m_user_valobj_sp is valid for.
  bool m_use_synthetic;

public:
  ValueObjectManager() {}
  
  ValueObjectManager(lldb::ValueObjectSP in_valobj_sp,
                     lldb::DynamicValueType use_dynamic, bool use_synthetic);
  
  bool IsValid() const;
  
  lldb::ValueObjectSP GetRootSP() const { return m_root_valobj_sp; }
  
  // Gets the correct value object from the root object for a given process
  // stop ID. If dynamic values are enabled, or if synthetic children are
  // enabled, the value object that the user wants to see might change while
  // debugging.
  lldb::ValueObjectSP GetSP();
  
  void SetUseDynamic(lldb::DynamicValueType use_dynamic);
  void SetUseSynthetic(bool use_synthetic);
  lldb::DynamicValueType GetUseDynamic() const { return m_use_dynamic; }
  bool GetUseSynthetic() const { return m_use_synthetic; }
  lldb::TargetSP GetTargetSP() const;
  lldb::ProcessSP GetProcessSP() const;
  lldb::ThreadSP GetThreadSP() const;
  lldb::StackFrameSP GetFrameSP() const;
};

} // namespace lldb_private

#endif // liblldb_ValueObject_h_
