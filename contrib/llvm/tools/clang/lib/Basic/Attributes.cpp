#include "clang/Basic/Attributes.h"
#include "clang/Basic/AttrSubjectMatchRules.h"
#include "clang/Basic/IdentifierTable.h"
#include "llvm/ADT/StringSwitch.h"
using namespace clang;

int clang::hasAttribute(AttrSyntax Syntax, const IdentifierInfo *Scope,
                        const IdentifierInfo *Attr, const TargetInfo &Target,
                        const LangOptions &LangOpts) {
  StringRef Name = Attr->getName();
  // Normalize the attribute name, __foo__ becomes foo.
  if (Name.size() >= 4 && Name.startswith("__") && Name.endswith("__"))
    Name = Name.substr(2, Name.size() - 4);

  // Normalize the scope name, but only for gnu and clang attributes.
  StringRef ScopeName = Scope ? Scope->getName() : "";
  if (ScopeName == "__gnu__")
    ScopeName = "gnu";
  else if (ScopeName == "_Clang")
    ScopeName = "clang";

#include "clang/Basic/AttrHasAttributeImpl.inc"

  return 0;
}

const char *attr::getSubjectMatchRuleSpelling(attr::SubjectMatchRule Rule) {
  switch (Rule) {
#define ATTR_MATCH_RULE(NAME, SPELLING, IsAbstract)                            \
  case attr::NAME:                                                             \
    return SPELLING;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
  llvm_unreachable("Invalid subject match rule");
}
