//===--- Diagnostic.td - C Language Family Diagnostic Handling ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen core definitions for the diagnostics
//  and diagnostic control.
//
//===----------------------------------------------------------------------===//

// See the Internals Manual, section The Diagnostics Subsystem for an overview.

// Define the diagnostic severities.
class Severity<string N> {
  string Name = N;
}
def SEV_Ignored : Severity<"Ignored">;
def SEV_Remark  : Severity<"Remark">;
def SEV_Warning : Severity<"Warning">;
def SEV_Error   : Severity<"Error">;
def SEV_Fatal   : Severity<"Fatal">;

// Define the diagnostic classes.
class DiagClass;
def CLASS_NOTE      : DiagClass;
def CLASS_REMARK    : DiagClass;
def CLASS_WARNING   : DiagClass;
def CLASS_EXTENSION : DiagClass;
def CLASS_ERROR     : DiagClass;

// Responses to a diagnostic in a SFINAE context.
class SFINAEResponse;
def SFINAE_SubstitutionFailure : SFINAEResponse;
def SFINAE_Suppress            : SFINAEResponse;
def SFINAE_Report              : SFINAEResponse;
def SFINAE_AccessControl       : SFINAEResponse;

// Textual substitutions which may be performed on the text of diagnostics
class TextSubstitution<string Text> {
  string Substitution = Text;
  // TODO: These are only here to allow substitutions to be declared inline with
  // diagnostics
  string Component = "";
  string CategoryName = "";
}

// Diagnostic Categories.  These can be applied to groups or individual
// diagnostics to specify a category.
class DiagCategory<string Name> {
  string CategoryName = Name;
}

// Diagnostic Groups.
class DiagGroup<string Name, list<DiagGroup> subgroups = []> {
  string GroupName = Name;
  list<DiagGroup> SubGroups = subgroups;
  string CategoryName = "";
  code Documentation = [{}];
}
class InGroup<DiagGroup G> { DiagGroup Group = G; }
//class IsGroup<string Name> { DiagGroup Group = DiagGroup<Name>; }

// This defines documentation for diagnostic groups.
include "DiagnosticDocs.td"

// This defines all of the named diagnostic categories.
include "DiagnosticCategories.td"

// This defines all of the named diagnostic groups.
include "DiagnosticGroups.td"


// All diagnostics emitted by the compiler are an indirect subclass of this.
class Diagnostic<string text, DiagClass DC, Severity defaultmapping> {
  /// Component is specified by the file with a big let directive.
  string         Component = ?;
  string         Text = text;
  DiagClass      Class = DC;
  SFINAEResponse SFINAE = SFINAE_Suppress;
  bit            AccessControl = 0;
  bit            WarningNoWerror = 0;
  bit            ShowInSystemHeader = 0;
  Severity       DefaultSeverity = defaultmapping;
  DiagGroup      Group;
  string         CategoryName = "";
}

class SFINAEFailure {
  SFINAEResponse SFINAE = SFINAE_SubstitutionFailure;
}
class NoSFINAE {
  SFINAEResponse SFINAE = SFINAE_Report;
}
class AccessControl {
  SFINAEResponse SFINAE = SFINAE_AccessControl;
}

class ShowInSystemHeader {
  bit ShowInSystemHeader = 1;
}

class SuppressInSystemHeader {
  bit ShowInSystemHeader = 0;
}

// FIXME: ExtWarn and Extension should also be SFINAEFailure by default.
class Error<string str>     : Diagnostic<str, CLASS_ERROR, SEV_Error>, SFINAEFailure {
  bit ShowInSystemHeader = 1;
}
// Warnings default to on (but can be default-off'd with DefaultIgnore).
// This is used for warnings about questionable code; warnings about
// accepted language extensions should use Extension or ExtWarn below instead.
class Warning<string str>   : Diagnostic<str, CLASS_WARNING, SEV_Warning>;
// Remarks can be turned on with -R flags and provide commentary, e.g. on
// optimizer decisions.
class Remark<string str>    : Diagnostic<str, CLASS_REMARK, SEV_Ignored>;
// Extensions are warnings about accepted language extensions.
// Extension warnings are default-off but enabled by -pedantic.
class Extension<string str> : Diagnostic<str, CLASS_EXTENSION, SEV_Ignored>;
// ExtWarns are warnings about accepted language extensions.
// ExtWarn warnings are default-on.
class ExtWarn<string str>   : Diagnostic<str, CLASS_EXTENSION, SEV_Warning>;
// Notes can provide supplementary information on errors, warnings, and remarks.
class Note<string str>      : Diagnostic<str, CLASS_NOTE, SEV_Fatal/*ignored*/>;


class DefaultIgnore { Severity DefaultSeverity = SEV_Ignored; }
class DefaultWarn   { Severity DefaultSeverity = SEV_Warning; }
class DefaultError  { Severity DefaultSeverity = SEV_Error; }
class DefaultFatal  { Severity DefaultSeverity = SEV_Fatal; }
class DefaultWarnNoWerror {
  bit WarningNoWerror = 1;
}
class DefaultRemark { Severity DefaultSeverity = SEV_Remark; }

// Definitions for Diagnostics.
include "DiagnosticASTKinds.td"
include "DiagnosticAnalysisKinds.td"
include "DiagnosticCommentKinds.td"
include "DiagnosticCommonKinds.td"
include "DiagnosticCrossTUKinds.td"
include "DiagnosticDriverKinds.td"
include "DiagnosticFrontendKinds.td"
include "DiagnosticLexKinds.td"
include "DiagnosticParseKinds.td"
include "DiagnosticRefactoringKinds.td"
include "DiagnosticSemaKinds.td"
include "DiagnosticSerializationKinds.td"

