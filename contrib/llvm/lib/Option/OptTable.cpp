//===- OptTable.cpp - Option Table Implementation -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::opt;

namespace llvm {
namespace opt {

// Ordering on Info. The ordering is *almost* case-insensitive lexicographic,
// with an exception. '\0' comes at the end of the alphabet instead of the
// beginning (thus options precede any other options which prefix them).
static int StrCmpOptionNameIgnoreCase(const char *A, const char *B) {
  const char *X = A, *Y = B;
  char a = tolower(*A), b = tolower(*B);
  while (a == b) {
    if (a == '\0')
      return 0;

    a = tolower(*++X);
    b = tolower(*++Y);
  }

  if (a == '\0') // A is a prefix of B.
    return 1;
  if (b == '\0') // B is a prefix of A.
    return -1;

  // Otherwise lexicographic.
  return (a < b) ? -1 : 1;
}

#ifndef NDEBUG
static int StrCmpOptionName(const char *A, const char *B) {
  if (int N = StrCmpOptionNameIgnoreCase(A, B))
    return N;
  return strcmp(A, B);
}

static inline bool operator<(const OptTable::Info &A, const OptTable::Info &B) {
  if (&A == &B)
    return false;

  if (int N = StrCmpOptionName(A.Name, B.Name))
    return N < 0;

  for (const char * const *APre = A.Prefixes,
                  * const *BPre = B.Prefixes;
                          *APre != nullptr && *BPre != nullptr; ++APre, ++BPre){
    if (int N = StrCmpOptionName(*APre, *BPre))
      return N < 0;
  }

  // Names are the same, check that classes are in order; exactly one
  // should be joined, and it should succeed the other.
  assert(((A.Kind == Option::JoinedClass) ^ (B.Kind == Option::JoinedClass)) &&
         "Unexpected classes for options with same name.");
  return B.Kind == Option::JoinedClass;
}
#endif

// Support lower_bound between info and an option name.
static inline bool operator<(const OptTable::Info &I, const char *Name) {
  return StrCmpOptionNameIgnoreCase(I.Name, Name) < 0;
}

} // end namespace opt
} // end namespace llvm

OptSpecifier::OptSpecifier(const Option *Opt) : ID(Opt->getID()) {}

OptTable::OptTable(ArrayRef<Info> OptionInfos, bool IgnoreCase)
    : OptionInfos(OptionInfos), IgnoreCase(IgnoreCase) {
  // Explicitly zero initialize the error to work around a bug in array
  // value-initialization on MinGW with gcc 4.3.5.

  // Find start of normal options.
  for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
    unsigned Kind = getInfo(i + 1).Kind;
    if (Kind == Option::InputClass) {
      assert(!TheInputOptionID && "Cannot have multiple input options!");
      TheInputOptionID = getInfo(i + 1).ID;
    } else if (Kind == Option::UnknownClass) {
      assert(!TheUnknownOptionID && "Cannot have multiple unknown options!");
      TheUnknownOptionID = getInfo(i + 1).ID;
    } else if (Kind != Option::GroupClass) {
      FirstSearchableIndex = i;
      break;
    }
  }
  assert(FirstSearchableIndex != 0 && "No searchable options?");

#ifndef NDEBUG
  // Check that everything after the first searchable option is a
  // regular option class.
  for (unsigned i = FirstSearchableIndex, e = getNumOptions(); i != e; ++i) {
    Option::OptionClass Kind = (Option::OptionClass) getInfo(i + 1).Kind;
    assert((Kind != Option::InputClass && Kind != Option::UnknownClass &&
            Kind != Option::GroupClass) &&
           "Special options should be defined first!");
  }

  // Check that options are in order.
  for (unsigned i = FirstSearchableIndex + 1, e = getNumOptions(); i != e; ++i){
    if (!(getInfo(i) < getInfo(i + 1))) {
      getOption(i).dump();
      getOption(i + 1).dump();
      llvm_unreachable("Options are not in order!");
    }
  }
#endif

  // Build prefixes.
  for (unsigned i = FirstSearchableIndex + 1, e = getNumOptions() + 1;
                i != e; ++i) {
    if (const char *const *P = getInfo(i).Prefixes) {
      for (; *P != nullptr; ++P) {
        PrefixesUnion.insert(*P);
      }
    }
  }

  // Build prefix chars.
  for (StringSet<>::const_iterator I = PrefixesUnion.begin(),
                                   E = PrefixesUnion.end(); I != E; ++I) {
    StringRef Prefix = I->getKey();
    for (StringRef::const_iterator C = Prefix.begin(), CE = Prefix.end();
                                   C != CE; ++C)
      if (!is_contained(PrefixChars, *C))
        PrefixChars.push_back(*C);
  }
}

OptTable::~OptTable() = default;

const Option OptTable::getOption(OptSpecifier Opt) const {
  unsigned id = Opt.getID();
  if (id == 0)
    return Option(nullptr, nullptr);
  assert((unsigned) (id - 1) < getNumOptions() && "Invalid ID.");
  return Option(&getInfo(id), this);
}

static bool isInput(const StringSet<> &Prefixes, StringRef Arg) {
  if (Arg == "-")
    return true;
  for (StringSet<>::const_iterator I = Prefixes.begin(),
                                   E = Prefixes.end(); I != E; ++I)
    if (Arg.startswith(I->getKey()))
      return false;
  return true;
}

/// \returns Matched size. 0 means no match.
static unsigned matchOption(const OptTable::Info *I, StringRef Str,
                            bool IgnoreCase) {
  for (const char * const *Pre = I->Prefixes; *Pre != nullptr; ++Pre) {
    StringRef Prefix(*Pre);
    if (Str.startswith(Prefix)) {
      StringRef Rest = Str.substr(Prefix.size());
      bool Matched = IgnoreCase
          ? Rest.startswith_lower(I->Name)
          : Rest.startswith(I->Name);
      if (Matched)
        return Prefix.size() + StringRef(I->Name).size();
    }
  }
  return 0;
}

// Returns true if one of the Prefixes + In.Names matches Option
static bool optionMatches(const OptTable::Info &In, StringRef Option) {
  if (In.Prefixes)
    for (size_t I = 0; In.Prefixes[I]; I++)
      if (Option == std::string(In.Prefixes[I]) + In.Name)
        return true;
  return false;
}

// This function is for flag value completion.
// Eg. When "-stdlib=" and "l" was passed to this function, it will return
// appropiriate values for stdlib, which starts with l.
std::vector<std::string>
OptTable::suggestValueCompletions(StringRef Option, StringRef Arg) const {
  // Search all options and return possible values.
  for (size_t I = FirstSearchableIndex, E = OptionInfos.size(); I < E; I++) {
    const Info &In = OptionInfos[I];
    if (!In.Values || !optionMatches(In, Option))
      continue;

    SmallVector<StringRef, 8> Candidates;
    StringRef(In.Values).split(Candidates, ",", -1, false);

    std::vector<std::string> Result;
    for (StringRef Val : Candidates)
      if (Val.startswith(Arg) && Arg.compare(Val))
        Result.push_back(Val);
    return Result;
  }
  return {};
}

std::vector<std::string>
OptTable::findByPrefix(StringRef Cur, unsigned short DisableFlags) const {
  std::vector<std::string> Ret;
  for (size_t I = FirstSearchableIndex, E = OptionInfos.size(); I < E; I++) {
    const Info &In = OptionInfos[I];
    if (!In.Prefixes || (!In.HelpText && !In.GroupID))
      continue;
    if (In.Flags & DisableFlags)
      continue;

    for (int I = 0; In.Prefixes[I]; I++) {
      std::string S = std::string(In.Prefixes[I]) + std::string(In.Name) + "\t";
      if (In.HelpText)
        S += In.HelpText;
      if (StringRef(S).startswith(Cur) && S.compare(std::string(Cur) + "\t"))
        Ret.push_back(S);
    }
  }
  return Ret;
}

unsigned OptTable::findNearest(StringRef Option, std::string &NearestString,
                               unsigned FlagsToInclude, unsigned FlagsToExclude,
                               unsigned MinimumLength) const {
  assert(!Option.empty());

  // Consider each option as a candidate, finding the closest match.
  unsigned BestDistance = UINT_MAX;
  for (const Info &CandidateInfo :
       ArrayRef<Info>(OptionInfos).drop_front(FirstSearchableIndex)) {
    StringRef CandidateName = CandidateInfo.Name;

    // Ignore option candidates with empty names, such as "--", or names
    // that do not meet the minimum length.
    if (CandidateName.empty() || CandidateName.size() < MinimumLength)
      continue;

    // If FlagsToInclude were specified, ignore options that don't include
    // those flags.
    if (FlagsToInclude && !(CandidateInfo.Flags & FlagsToInclude))
      continue;
    // Ignore options that contain the FlagsToExclude.
    if (CandidateInfo.Flags & FlagsToExclude)
      continue;

    // Ignore positional argument option candidates (which do not
    // have prefixes).
    if (!CandidateInfo.Prefixes)
      continue;
    // Find the most appropriate prefix. For example, if a user asks for
    // "--helm", suggest "--help" over "-help".
    StringRef Prefix = CandidateInfo.Prefixes[0];
    for (int P = 1; CandidateInfo.Prefixes[P]; P++) {
      if (Option.startswith(CandidateInfo.Prefixes[P]))
        Prefix = CandidateInfo.Prefixes[P];
    }

    // Check if the candidate ends with a character commonly used when
    // delimiting an option from its value, such as '=' or ':'. If it does,
    // attempt to split the given option based on that delimiter.
    std::string Delimiter = "";
    char Last = CandidateName.back();
    if (Last == '=' || Last == ':')
      Delimiter = std::string(1, Last);

    StringRef LHS, RHS;
    if (Delimiter.empty())
      LHS = Option;
    else
      std::tie(LHS, RHS) = Option.split(Last);

    std::string NormalizedName =
        (LHS.drop_front(Prefix.size()) + Delimiter).str();
    unsigned Distance =
        CandidateName.edit_distance(NormalizedName, /*AllowReplacements=*/true,
                                    /*MaxEditDistance=*/BestDistance);
    if (Distance < BestDistance) {
      BestDistance = Distance;
      NearestString = (Prefix + CandidateName + RHS).str();
    }
  }
  return BestDistance;
}

bool OptTable::addValues(const char *Option, const char *Values) {
  for (size_t I = FirstSearchableIndex, E = OptionInfos.size(); I < E; I++) {
    Info &In = OptionInfos[I];
    if (optionMatches(In, Option)) {
      In.Values = Values;
      return true;
    }
  }
  return false;
}

Arg *OptTable::ParseOneArg(const ArgList &Args, unsigned &Index,
                           unsigned FlagsToInclude,
                           unsigned FlagsToExclude) const {
  unsigned Prev = Index;
  const char *Str = Args.getArgString(Index);

  // Anything that doesn't start with PrefixesUnion is an input, as is '-'
  // itself.
  if (isInput(PrefixesUnion, Str))
    return new Arg(getOption(TheInputOptionID), Str, Index++, Str);

  const Info *Start = OptionInfos.data() + FirstSearchableIndex;
  const Info *End = OptionInfos.data() + OptionInfos.size();
  StringRef Name = StringRef(Str).ltrim(PrefixChars);

  // Search for the first next option which could be a prefix.
  Start = std::lower_bound(Start, End, Name.data());

  // Options are stored in sorted order, with '\0' at the end of the
  // alphabet. Since the only options which can accept a string must
  // prefix it, we iteratively search for the next option which could
  // be a prefix.
  //
  // FIXME: This is searching much more than necessary, but I am
  // blanking on the simplest way to make it fast. We can solve this
  // problem when we move to TableGen.
  for (; Start != End; ++Start) {
    unsigned ArgSize = 0;
    // Scan for first option which is a proper prefix.
    for (; Start != End; ++Start)
      if ((ArgSize = matchOption(Start, Str, IgnoreCase)))
        break;
    if (Start == End)
      break;

    Option Opt(Start, this);

    if (FlagsToInclude && !Opt.hasFlag(FlagsToInclude))
      continue;
    if (Opt.hasFlag(FlagsToExclude))
      continue;

    // See if this option matches.
    if (Arg *A = Opt.accept(Args, Index, ArgSize))
      return A;

    // Otherwise, see if this argument was missing values.
    if (Prev != Index)
      return nullptr;
  }

  // If we failed to find an option and this arg started with /, then it's
  // probably an input path.
  if (Str[0] == '/')
    return new Arg(getOption(TheInputOptionID), Str, Index++, Str);

  return new Arg(getOption(TheUnknownOptionID), Str, Index++, Str);
}

InputArgList OptTable::ParseArgs(ArrayRef<const char *> ArgArr,
                                 unsigned &MissingArgIndex,
                                 unsigned &MissingArgCount,
                                 unsigned FlagsToInclude,
                                 unsigned FlagsToExclude) const {
  InputArgList Args(ArgArr.begin(), ArgArr.end());

  // FIXME: Handle '@' args (or at least error on them).

  MissingArgIndex = MissingArgCount = 0;
  unsigned Index = 0, End = ArgArr.size();
  while (Index < End) {
    // Ingore nullptrs, they are response file's EOL markers
    if (Args.getArgString(Index) == nullptr) {
      ++Index;
      continue;
    }
    // Ignore empty arguments (other things may still take them as arguments).
    StringRef Str = Args.getArgString(Index);
    if (Str == "") {
      ++Index;
      continue;
    }

    unsigned Prev = Index;
    Arg *A = ParseOneArg(Args, Index, FlagsToInclude, FlagsToExclude);
    assert(Index > Prev && "Parser failed to consume argument.");

    // Check for missing argument error.
    if (!A) {
      assert(Index >= End && "Unexpected parser error.");
      assert(Index - Prev - 1 && "No missing arguments!");
      MissingArgIndex = Prev;
      MissingArgCount = Index - Prev - 1;
      break;
    }

    Args.append(A);
  }

  return Args;
}

static std::string getOptionHelpName(const OptTable &Opts, OptSpecifier Id) {
  const Option O = Opts.getOption(Id);
  std::string Name = O.getPrefixedName();

  // Add metavar, if used.
  switch (O.getKind()) {
  case Option::GroupClass: case Option::InputClass: case Option::UnknownClass:
    llvm_unreachable("Invalid option with help text.");

  case Option::MultiArgClass:
    if (const char *MetaVarName = Opts.getOptionMetaVar(Id)) {
      // For MultiArgs, metavar is full list of all argument names.
      Name += ' ';
      Name += MetaVarName;
    }
    else {
      // For MultiArgs<N>, if metavar not supplied, print <value> N times.
      for (unsigned i=0, e=O.getNumArgs(); i< e; ++i) {
        Name += " <value>";
      }
    }
    break;

  case Option::FlagClass:
    break;

  case Option::ValuesClass:
    break;

  case Option::SeparateClass: case Option::JoinedOrSeparateClass:
  case Option::RemainingArgsClass: case Option::RemainingArgsJoinedClass:
    Name += ' ';
    LLVM_FALLTHROUGH;
  case Option::JoinedClass: case Option::CommaJoinedClass:
  case Option::JoinedAndSeparateClass:
    if (const char *MetaVarName = Opts.getOptionMetaVar(Id))
      Name += MetaVarName;
    else
      Name += "<value>";
    break;
  }

  return Name;
}

namespace {
struct OptionInfo {
  std::string Name;
  StringRef HelpText;
};
} // namespace

static void PrintHelpOptionList(raw_ostream &OS, StringRef Title,
                                std::vector<OptionInfo> &OptionHelp) {
  OS << Title << ":\n";

  // Find the maximum option length.
  unsigned OptionFieldWidth = 0;
  for (unsigned i = 0, e = OptionHelp.size(); i != e; ++i) {
    // Limit the amount of padding we are willing to give up for alignment.
    unsigned Length = OptionHelp[i].Name.size();
    if (Length <= 23)
      OptionFieldWidth = std::max(OptionFieldWidth, Length);
  }

  const unsigned InitialPad = 2;
  for (unsigned i = 0, e = OptionHelp.size(); i != e; ++i) {
    const std::string &Option = OptionHelp[i].Name;
    int Pad = OptionFieldWidth - int(Option.size());
    OS.indent(InitialPad) << Option;

    // Break on long option names.
    if (Pad < 0) {
      OS << "\n";
      Pad = OptionFieldWidth + InitialPad;
    }
    OS.indent(Pad + 1) << OptionHelp[i].HelpText << '\n';
  }
}

static const char *getOptionHelpGroup(const OptTable &Opts, OptSpecifier Id) {
  unsigned GroupID = Opts.getOptionGroupID(Id);

  // If not in a group, return the default help group.
  if (!GroupID)
    return "OPTIONS";

  // Abuse the help text of the option groups to store the "help group"
  // name.
  //
  // FIXME: Split out option groups.
  if (const char *GroupHelp = Opts.getOptionHelpText(GroupID))
    return GroupHelp;

  // Otherwise keep looking.
  return getOptionHelpGroup(Opts, GroupID);
}

void OptTable::PrintHelp(raw_ostream &OS, const char *Usage, const char *Title,
                         bool ShowHidden, bool ShowAllAliases) const {
  PrintHelp(OS, Usage, Title, /*Include*/ 0, /*Exclude*/
            (ShowHidden ? 0 : HelpHidden), ShowAllAliases);
}

void OptTable::PrintHelp(raw_ostream &OS, const char *Usage, const char *Title,
                         unsigned FlagsToInclude, unsigned FlagsToExclude,
                         bool ShowAllAliases) const {
  OS << "OVERVIEW: " << Title << "\n\n";
  OS << "USAGE: " << Usage << "\n\n";

  // Render help text into a map of group-name to a list of (option, help)
  // pairs.
  std::map<std::string, std::vector<OptionInfo>> GroupedOptionHelp;

  for (unsigned Id = 1, e = getNumOptions() + 1; Id != e; ++Id) {
    // FIXME: Split out option groups.
    if (getOptionKind(Id) == Option::GroupClass)
      continue;

    unsigned Flags = getInfo(Id).Flags;
    if (FlagsToInclude && !(Flags & FlagsToInclude))
      continue;
    if (Flags & FlagsToExclude)
      continue;

    // If an alias doesn't have a help text, show a help text for the aliased
    // option instead.
    const char *HelpText = getOptionHelpText(Id);
    if (!HelpText && ShowAllAliases) {
      const Option Alias = getOption(Id).getAlias();
      if (Alias.isValid())
        HelpText = getOptionHelpText(Alias.getID());
    }

    if (HelpText) {
      const char *HelpGroup = getOptionHelpGroup(*this, Id);
      const std::string &OptName = getOptionHelpName(*this, Id);
      GroupedOptionHelp[HelpGroup].push_back({OptName, HelpText});
    }
  }

  for (auto& OptionGroup : GroupedOptionHelp) {
    if (OptionGroup.first != GroupedOptionHelp.begin()->first)
      OS << "\n";
    PrintHelpOptionList(OS, OptionGroup.first, OptionGroup.second);
  }

  OS.flush();
}
