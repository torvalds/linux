//===-- MICmdArgValOptionLong.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmdArgValListBase.h"

// Declarations:
class CMICmdArgContext;
class CMIUtilString;

//++
//============================================================================
// Details: MI common code class. Command argument class. Arguments object
//          needing specialization derived from the CMICmdArgValBase class.
//          An argument knows what type of argument it is and how it is to
//          interpret the options (context) string to find and validate a
//          matching
//          argument and so extract a value from it.
//          If *this argument has expected options following it the option
//          objects
//          created to hold each of those option's values belong to *this
//          argument
//          object and so are deleted when *this object goes out of scope.
//          Based on the Interpreter pattern.
//--
class CMICmdArgValOptionLong : public CMICmdArgValListBase {
  // Methods:
public:
  /* ctor */ CMICmdArgValOptionLong();
  /* ctor */ CMICmdArgValOptionLong(const CMIUtilString &vrArgName,
                                    const bool vbMandatory,
                                    const bool vbHandleByCmd);
  /* ctor */ CMICmdArgValOptionLong(const CMIUtilString &vrArgName,
                                    const bool vbMandatory,
                                    const bool vbHandleByCmd,
                                    const ArgValType_e veType,
                                    const MIuint vnExpectingNOptions);
  //
  bool IsArgLongOption(const CMIUtilString &vrTxt) const;
  const VecArgObjPtr_t &GetExpectedOptions() const;
  template <class T1, typename T2> bool GetExpectedOption(T2 &vrwValue) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValOptionLong() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vArgContext) override;

  // Methods:
protected:
  bool ExtractExpectedOptions(CMICmdArgContext &vrwTxt, const MIuint nArgIndex);

  // Overrideable:
protected:
  virtual bool IsArgOptionCorrect(const CMIUtilString &vrTxt) const;
  virtual bool ArgNameMatch(const CMIUtilString &vrTxt) const;

  // Methods:
private:
  void Destroy();

  // Attributes:
private:
  MIuint m_nExpectingNOptions;         // The number of options expected to read
                                       // following *this argument
  VecArgObjPtr_t m_vecArgsExpected;    // The option objects holding the value
                                       // extracted following *this argument
  ArgValType_e m_eExpectingOptionType; // The type of options expected to read
                                       // following *this argument
};

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the first argument or option value from the list of 1 or
// more options
//          parsed from the command's options string.
// Type:    Template method.
// Args:    vrwValue    - (W) Templated type return value.
//          T1          - The argument value's class type of the data hold in
//          the list of options.
//          T2          - The type pf the variable which holds the value wanted.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed. List of object was empty.
// Throws:  None.
//--
template <class T1, typename T2>
bool CMICmdArgValOptionLong::GetExpectedOption(T2 &vrwValue) const {
  const VecArgObjPtr_t &rVecOptions(GetExpectedOptions());
  VecArgObjPtr_t::const_iterator it2 = rVecOptions.begin();
  if (it2 != rVecOptions.end()) {
    const T1 *pOption = static_cast<T1 *>(*it2);
    vrwValue = pOption->GetValue();
    return MIstatus::success;
  }

  return MIstatus::failure;
}
