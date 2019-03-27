//===-- MICmdArgValListOfN.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include <vector>

// In-house headers:
#include "MICmdArgValListBase.h"

// Declarations:
class CMICmdArgContext;

//++
//============================================================================
// Details: MI common code class. Command argument class. Arguments object
//          needing specialization derived from the CMICmdArgValBase class.
//          An argument knows what type of argument it is and how it is to
//          interpret the options (context) string to find and validate a
//          matching
//          argument and so extract a value from it .
//          The CMICmdArgValBase objects added to *this ListOfN container belong
//          to this container and will be deleted when *this object goes out of
//          scope.
//          To parse arguments like 'thread-id ...' i.e. 1 10 12 13 ...
//          If vbMandatory argument is true it takes on the (...)+ specification
//          otherwise assumed to be (...)* specification.
//          Based on the Interpreter pattern.
//--
class CMICmdArgValListOfN : public CMICmdArgValListBase {
  // Methods:
public:
  /* ctor */ CMICmdArgValListOfN();
  /* ctor */ CMICmdArgValListOfN(const CMIUtilString &vrArgName,
                                 const bool vbMandatory,
                                 const bool vbHandleByCmd,
                                 const ArgValType_e veType);
  //
  const VecArgObjPtr_t &GetExpectedOptions() const;
  template <class T1, typename T2>
  bool GetExpectedOption(T2 &vrwValue,
                         const VecArgObjPtr_t::size_type vnAt = 0) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValListOfN() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vArgContext) override;

  // Methods:
private:
  bool IsListOfN(const CMIUtilString &vrTxt) const;
  bool CreateList(const CMIUtilString &vrTxt);
};

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the first argument or option value from the list of 1 or
// more options
//          parsed from the command's options string.
// Type:    Template method.
// Args:    vrwValue    - (W) Templated type return value.
//          vnAt        - (R) Value at the specific position.
//          T1          - The argument value's class type of the data hold in
//          the list of options.
//          T2          - The type pf the variable which holds the value wanted.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed. List of object was empty.
// Throws:  None.
//--
template <class T1, typename T2>
bool CMICmdArgValListOfN::GetExpectedOption(
    T2 &vrwValue, const VecArgObjPtr_t::size_type vnAt) const {
  const VecArgObjPtr_t &rVecOptions(GetExpectedOptions());
  if (rVecOptions.size() <= vnAt)
    return MIstatus::failure;

  VecArgObjPtr_t::const_iterator it2 = rVecOptions.begin() + vnAt;
  if (it2 != rVecOptions.end()) {
    const T1 *pOption = static_cast<T1 *>(*it2);
    vrwValue = pOption->GetValue();
    return MIstatus::success;
  }

  return MIstatus::failure;
}
