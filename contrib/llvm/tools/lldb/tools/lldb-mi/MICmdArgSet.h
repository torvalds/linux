//===-- MICmdArgSet.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include "MICmdArgContext.h"
#include "MICmnBase.h"

// Declarations:
class CMICmdArgValBase;

//++
//============================================================================
// Details: MI common code class. Command arguments container class.
//          A command may have one or more arguments of which some may be
//          optional.
//          *this class contains a list of the command's arguments which are
//          validates against the commands argument options string (context
//          string).
//          Each argument tries to extract the value it is looking for.
//          Argument objects added to *this container are owned by this
//          container
//          and are deleted when this container goes out of scope. Allocate
//          argument
//          objects on the heap.
//          It is assumed the arguments to be parsed are read from left to right
//          in
//          order. The order added to *this container is the order they will
//          parsed.
//--
class CMICmdArgSet : public CMICmnBase {
  // Classes:
public:
  //++
  // Description: ArgSet's interface for command arguments to implement.
  //--
  class IArg {
  public:
    virtual bool GetFound() const = 0;
    virtual bool GetIsHandledByCmd() const = 0;
    virtual bool GetIsMandatory() const = 0;
    virtual bool GetIsMissingOptions() const = 0;
    virtual const CMIUtilString &GetName() const = 0;
    virtual bool GetValid() const = 0;
    virtual bool Validate(CMICmdArgContext &vwArgContext) = 0;

    virtual ~IArg() = default;
  };

  // Typedefs:
  typedef std::vector<CMICmdArgValBase *> SetCmdArgs_t;

  // Methods:
  CMICmdArgSet();

  void Add(CMICmdArgValBase *vArg);
  bool GetArg(const CMIUtilString &vArgName, CMICmdArgValBase *&vpArg) const;
  const SetCmdArgs_t &GetArgsThatAreMissing() const;
  const SetCmdArgs_t &GetArgsThatInvalid() const;
  size_t GetCount() const;
  bool IsArgContextEmpty() const;
  bool IsArgsPresentButNotHandledByCmd() const;
  void WarningArgsNotHandledbyCmdLogFile(const CMIUtilString &vrCmdName);
  bool Validate(const CMIUtilString &vStrMiCmd,
                CMICmdArgContext &vwCmdArgsText);

  // Overrideable:
  ~CMICmdArgSet() override;

  // Methods:
private:
  const SetCmdArgs_t &GetArgsNotHandledByCmd() const;
  void Destroy(); // Release resources used by *this object
  bool ValidationFormErrorMessages(const CMICmdArgContext &vwCmdArgsText);

  // Attributes:
  bool m_bIsArgsPresentButNotHandledByCmd; // True = The driver's client
                                           // presented the command with options
                                           // recognised but not handled by
  // a command, false = all args handled
  SetCmdArgs_t m_setCmdArgs; // The set of arguments that are that the command
                             // is expecting to find in the options string
  SetCmdArgs_t m_setCmdArgsThatAreMissing; // The set of arguments that are
                                           // required by the command but are
                                           // missing
  SetCmdArgs_t m_setCmdArgsThatNotValid;   // The set of arguments found in the
                                           // text but for some reason unable to
                                           // extract a value
  SetCmdArgs_t m_setCmdArgsNotHandledByCmd; // The set of arguments specified by
                                            // the command which were present to
                                            // the command but not handled
  SetCmdArgs_t m_setCmdArgsMissingInfo;     // The set of arguments that were
                                        // present but were found to be missing
                                        // additional information i.e.
  // --thread 3 but 3 is missing
  CMICmdArgContext m_cmdArgContext; // Copy of the command's argument options
                                    // text before validate takes place (empties
                                    // it of content)
  const CMIUtilString m_constStrCommaSpc;
};
