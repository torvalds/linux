//== PrintfFormatString.cpp - Analysis of printf format strings --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Handling of format string in printf and friends.  The structure of format
// strings for fprintf() are described in C99 7.19.6.1.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/FormatString.h"
#include "clang/AST/OSLog.h"
#include "FormatStringParsing.h"
#include "clang/Basic/TargetInfo.h"

using clang::analyze_format_string::ArgType;
using clang::analyze_format_string::FormatStringHandler;
using clang::analyze_format_string::LengthModifier;
using clang::analyze_format_string::OptionalAmount;
using clang::analyze_format_string::ConversionSpecifier;
using clang::analyze_printf::PrintfSpecifier;

using namespace clang;

typedef clang::analyze_format_string::SpecifierResult<PrintfSpecifier>
        PrintfSpecifierResult;

//===----------------------------------------------------------------------===//
// Methods for parsing format strings.
//===----------------------------------------------------------------------===//

using analyze_format_string::ParseNonPositionAmount;

static bool ParsePrecision(FormatStringHandler &H, PrintfSpecifier &FS,
                           const char *Start, const char *&Beg, const char *E,
                           unsigned *argIndex) {
  if (argIndex) {
    FS.setPrecision(ParseNonPositionAmount(Beg, E, *argIndex));
  } else {
    const OptionalAmount Amt = ParsePositionAmount(H, Start, Beg, E,
                                           analyze_format_string::PrecisionPos);
    if (Amt.isInvalid())
      return true;
    FS.setPrecision(Amt);
  }
  return false;
}

static bool ParseObjCFlags(FormatStringHandler &H, PrintfSpecifier &FS,
                           const char *FlagBeg, const char *E, bool Warn) {
   StringRef Flag(FlagBeg, E - FlagBeg);
   // Currently there is only one flag.
   if (Flag == "tt") {
     FS.setHasObjCTechnicalTerm(FlagBeg);
     return false;
   }
   // Handle either the case of no flag or an invalid flag.
   if (Warn) {
     if (Flag == "")
       H.HandleEmptyObjCModifierFlag(FlagBeg, E  - FlagBeg);
     else
       H.HandleInvalidObjCModifierFlag(FlagBeg, E  - FlagBeg);
   }
   return true;
}

static PrintfSpecifierResult ParsePrintfSpecifier(FormatStringHandler &H,
                                                  const char *&Beg,
                                                  const char *E,
                                                  unsigned &argIndex,
                                                  const LangOptions &LO,
                                                  const TargetInfo &Target,
                                                  bool Warn,
                                                  bool isFreeBSDKPrintf) {

  using namespace clang::analyze_format_string;
  using namespace clang::analyze_printf;

  const char *I = Beg;
  const char *Start = nullptr;
  UpdateOnReturn <const char*> UpdateBeg(Beg, I);

  // Look for a '%' character that indicates the start of a format specifier.
  for ( ; I != E ; ++I) {
    char c = *I;
    if (c == '\0') {
      // Detect spurious null characters, which are likely errors.
      H.HandleNullChar(I);
      return true;
    }
    if (c == '%') {
      Start = I++;  // Record the start of the format specifier.
      break;
    }
  }

  // No format specifier found?
  if (!Start)
    return false;

  if (I == E) {
    // No more characters left?
    if (Warn)
      H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  PrintfSpecifier FS;
  if (ParseArgPosition(H, FS, Start, I, E))
    return true;

  if (I == E) {
    // No more characters left?
    if (Warn)
      H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  if (*I == '{') {
    ++I;
    unsigned char PrivacyFlags = 0;
    StringRef MatchedStr;

    do {
      StringRef Str(I, E - I);
      std::string Match = "^[[:space:]]*"
                          "(private|public|sensitive|mask\\.[^[:space:],}]*)"
                          "[[:space:]]*(,|})";
      llvm::Regex R(Match);
      SmallVector<StringRef, 2> Matches;

      if (R.match(Str, &Matches)) {
        MatchedStr = Matches[1];
        I += Matches[0].size();

        // Set the privacy flag if the privacy annotation in the
        // comma-delimited segment is at least as strict as the privacy
        // annotations in previous comma-delimited segments.
        if (MatchedStr.startswith("mask")) {
          StringRef MaskType = MatchedStr.substr(sizeof("mask.") - 1);
          unsigned Size = MaskType.size();
          if (Warn && (Size == 0 || Size > 8))
            H.handleInvalidMaskType(MaskType);
          FS.setMaskType(MaskType);
        } else if (MatchedStr.equals("sensitive"))
          PrivacyFlags = clang::analyze_os_log::OSLogBufferItem::IsSensitive;
        else if (PrivacyFlags !=
                 clang::analyze_os_log::OSLogBufferItem::IsSensitive &&
                 MatchedStr.equals("private"))
          PrivacyFlags = clang::analyze_os_log::OSLogBufferItem::IsPrivate;
        else if (PrivacyFlags == 0 && MatchedStr.equals("public"))
          PrivacyFlags = clang::analyze_os_log::OSLogBufferItem::IsPublic;
      } else {
        size_t CommaOrBracePos =
            Str.find_if([](char c) { return c == ',' || c == '}'; });

        if (CommaOrBracePos == StringRef::npos) {
          // Neither a comma nor the closing brace was found.
          if (Warn)
            H.HandleIncompleteSpecifier(Start, E - Start);
          return true;
        }

        I += CommaOrBracePos + 1;
      }
      // Continue until the closing brace is found.
    } while (*(I - 1) == ',');

    // Set the privacy flag.
    switch (PrivacyFlags) {
    case 0:
      break;
    case clang::analyze_os_log::OSLogBufferItem::IsPrivate:
      FS.setIsPrivate(MatchedStr.data());
      break;
    case clang::analyze_os_log::OSLogBufferItem::IsPublic:
      FS.setIsPublic(MatchedStr.data());
      break;
    case clang::analyze_os_log::OSLogBufferItem::IsSensitive:
      FS.setIsSensitive(MatchedStr.data());
      break;
    default:
      llvm_unreachable("Unexpected privacy flag value");
    }
  }

  // Look for flags (if any).
  bool hasMore = true;
  for ( ; I != E; ++I) {
    switch (*I) {
      default: hasMore = false; break;
      case '\'':
        // FIXME: POSIX specific.  Always accept?
        FS.setHasThousandsGrouping(I);
        break;
      case '-': FS.setIsLeftJustified(I); break;
      case '+': FS.setHasPlusPrefix(I); break;
      case ' ': FS.setHasSpacePrefix(I); break;
      case '#': FS.setHasAlternativeForm(I); break;
      case '0': FS.setHasLeadingZeros(I); break;
    }
    if (!hasMore)
      break;
  }

  if (I == E) {
    // No more characters left?
    if (Warn)
      H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for the field width (if any).
  if (ParseFieldWidth(H, FS, Start, I, E,
                      FS.usesPositionalArg() ? nullptr : &argIndex))
    return true;

  if (I == E) {
    // No more characters left?
    if (Warn)
      H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for the precision (if any).
  if (*I == '.') {
    ++I;
    if (I == E) {
      if (Warn)
        H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }

    if (ParsePrecision(H, FS, Start, I, E,
                       FS.usesPositionalArg() ? nullptr : &argIndex))
      return true;

    if (I == E) {
      // No more characters left?
      if (Warn)
        H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }
  }

  if (ParseVectorModifier(H, FS, I, E, LO))
    return true;

  // Look for the length modifier.
  if (ParseLengthModifier(FS, I, E, LO) && I == E) {
    // No more characters left?
    if (Warn)
      H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for the Objective-C modifier flags, if any.
  // We parse these here, even if they don't apply to
  // the conversion specifier, and then emit an error
  // later if the conversion specifier isn't '@'.  This
  // enables better recovery, and we don't know if
  // these flags are applicable until later.
  const char *ObjCModifierFlagsStart = nullptr,
             *ObjCModifierFlagsEnd = nullptr;
  if (*I == '[') {
    ObjCModifierFlagsStart = I;
    ++I;
    auto flagStart = I;
    for (;; ++I) {
      ObjCModifierFlagsEnd = I;
      if (I == E) {
        if (Warn)
          H.HandleIncompleteSpecifier(Start, E - Start);
        return true;
      }
      // Did we find the closing ']'?
      if (*I == ']') {
        if (ParseObjCFlags(H, FS, flagStart, I, Warn))
          return true;
        ++I;
        break;
      }
      // There are no separators defined yet for multiple
      // Objective-C modifier flags.  When those are
      // defined, this is the place to check.
    }
  }

  if (*I == '\0') {
    // Detect spurious null characters, which are likely errors.
    H.HandleNullChar(I);
    return true;
  }

  // Finally, look for the conversion specifier.
  const char *conversionPosition = I++;
  ConversionSpecifier::Kind k = ConversionSpecifier::InvalidSpecifier;
  switch (*conversionPosition) {
    default:
      break;
    // C99: 7.19.6.1 (section 8).
    case '%': k = ConversionSpecifier::PercentArg;   break;
    case 'A': k = ConversionSpecifier::AArg; break;
    case 'E': k = ConversionSpecifier::EArg; break;
    case 'F': k = ConversionSpecifier::FArg; break;
    case 'G': k = ConversionSpecifier::GArg; break;
    case 'X': k = ConversionSpecifier::XArg; break;
    case 'a': k = ConversionSpecifier::aArg; break;
    case 'c': k = ConversionSpecifier::cArg; break;
    case 'd': k = ConversionSpecifier::dArg; break;
    case 'e': k = ConversionSpecifier::eArg; break;
    case 'f': k = ConversionSpecifier::fArg; break;
    case 'g': k = ConversionSpecifier::gArg; break;
    case 'i': k = ConversionSpecifier::iArg; break;
    case 'n': k = ConversionSpecifier::nArg; break;
    case 'o': k = ConversionSpecifier::oArg; break;
    case 'p': k = ConversionSpecifier::pArg; break;
    case 's': k = ConversionSpecifier::sArg; break;
    case 'u': k = ConversionSpecifier::uArg; break;
    case 'x': k = ConversionSpecifier::xArg; break;
    // POSIX specific.
    case 'C': k = ConversionSpecifier::CArg; break;
    case 'S': k = ConversionSpecifier::SArg; break;
    // Apple extension for os_log
    case 'P':
      k = ConversionSpecifier::PArg;
      break;
    // Objective-C.
    case '@': k = ConversionSpecifier::ObjCObjArg; break;
    // Glibc specific.
    case 'm': k = ConversionSpecifier::PrintErrno; break;
    // FreeBSD kernel specific.
    case 'b':
      if (isFreeBSDKPrintf)
        k = ConversionSpecifier::FreeBSDbArg; // int followed by char *
      break;
    case 'r':
      if (isFreeBSDKPrintf)
        k = ConversionSpecifier::FreeBSDrArg; // int
      break;
    case 'y':
      if (isFreeBSDKPrintf)
        k = ConversionSpecifier::FreeBSDyArg; // int
      break;
    // Apple-specific.
    case 'D':
      if (isFreeBSDKPrintf)
        k = ConversionSpecifier::FreeBSDDArg; // void * followed by char *
      else if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::DArg;
      break;
    case 'O':
      if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::OArg;
      break;
    case 'U':
      if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::UArg;
      break;
    // MS specific.
    case 'Z':
      if (Target.getTriple().isOSMSVCRT())
        k = ConversionSpecifier::ZArg;
      break;
  }

  // Check to see if we used the Objective-C modifier flags with
  // a conversion specifier other than '@'.
  if (k != ConversionSpecifier::ObjCObjArg &&
      k != ConversionSpecifier::InvalidSpecifier &&
      ObjCModifierFlagsStart) {
    H.HandleObjCFlagsWithNonObjCConversion(ObjCModifierFlagsStart,
                                           ObjCModifierFlagsEnd + 1,
                                           conversionPosition);
    return true;
  }

  PrintfConversionSpecifier CS(conversionPosition, k);
  FS.setConversionSpecifier(CS);
  if (CS.consumesDataArgument() && !FS.usesPositionalArg())
    FS.setArgIndex(argIndex++);
  // FreeBSD kernel specific.
  if (k == ConversionSpecifier::FreeBSDbArg ||
      k == ConversionSpecifier::FreeBSDDArg)
    argIndex++;

  if (k == ConversionSpecifier::InvalidSpecifier) {
    unsigned Len = I - Start;
    if (ParseUTF8InvalidSpecifier(Start, E, Len)) {
      CS.setEndScanList(Start + Len);
      FS.setConversionSpecifier(CS);
    }
    // Assume the conversion takes one argument.
    return !H.HandleInvalidPrintfConversionSpecifier(FS, Start, Len);
  }
  return PrintfSpecifierResult(Start, FS);
}

bool clang::analyze_format_string::ParsePrintfString(FormatStringHandler &H,
                                                     const char *I,
                                                     const char *E,
                                                     const LangOptions &LO,
                                                     const TargetInfo &Target,
                                                     bool isFreeBSDKPrintf) {

  unsigned argIndex = 0;

  // Keep looking for a format specifier until we have exhausted the string.
  while (I != E) {
    const PrintfSpecifierResult &FSR = ParsePrintfSpecifier(H, I, E, argIndex,
                                                            LO, Target, true,
                                                            isFreeBSDKPrintf);
    // Did a fail-stop error of any kind occur when parsing the specifier?
    // If so, don't do any more processing.
    if (FSR.shouldStop())
      return true;
    // Did we exhaust the string or encounter an error that
    // we can recover from?
    if (!FSR.hasValue())
      continue;
    // We have a format specifier.  Pass it to the callback.
    if (!H.HandlePrintfSpecifier(FSR.getValue(), FSR.getStart(),
                                 I - FSR.getStart()))
      return true;
  }
  assert(I == E && "Format string not exhausted");
  return false;
}

bool clang::analyze_format_string::ParseFormatStringHasSArg(const char *I,
                                                            const char *E,
                                                            const LangOptions &LO,
                                                            const TargetInfo &Target) {

  unsigned argIndex = 0;

  // Keep looking for a %s format specifier until we have exhausted the string.
  FormatStringHandler H;
  while (I != E) {
    const PrintfSpecifierResult &FSR = ParsePrintfSpecifier(H, I, E, argIndex,
                                                            LO, Target, false,
                                                            false);
    // Did a fail-stop error of any kind occur when parsing the specifier?
    // If so, don't do any more processing.
    if (FSR.shouldStop())
      return false;
    // Did we exhaust the string or encounter an error that
    // we can recover from?
    if (!FSR.hasValue())
      continue;
    const analyze_printf::PrintfSpecifier &FS = FSR.getValue();
    // Return true if this a %s format specifier.
    if (FS.getConversionSpecifier().getKind() == ConversionSpecifier::Kind::sArg)
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Methods on PrintfSpecifier.
//===----------------------------------------------------------------------===//

ArgType PrintfSpecifier::getScalarArgType(ASTContext &Ctx,
                                          bool IsObjCLiteral) const {
  if (CS.getKind() == ConversionSpecifier::cArg)
    switch (LM.getKind()) {
      case LengthModifier::None:
        return Ctx.IntTy;
      case LengthModifier::AsLong:
      case LengthModifier::AsWide:
        return ArgType(ArgType::WIntTy, "wint_t");
      case LengthModifier::AsShort:
        if (Ctx.getTargetInfo().getTriple().isOSMSVCRT())
          return Ctx.IntTy;
        LLVM_FALLTHROUGH;
      default:
        return ArgType::Invalid();
    }

  if (CS.isIntArg())
    switch (LM.getKind()) {
      case LengthModifier::AsLongDouble:
        // GNU extension.
        return Ctx.LongLongTy;
      case LengthModifier::None:
        return Ctx.IntTy;
      case LengthModifier::AsInt32:
        return ArgType(Ctx.IntTy, "__int32");
      case LengthModifier::AsChar: return ArgType::AnyCharTy;
      case LengthModifier::AsShort: return Ctx.ShortTy;
      case LengthModifier::AsLong: return Ctx.LongTy;
      case LengthModifier::AsLongLong:
      case LengthModifier::AsQuad:
        return Ctx.LongLongTy;
      case LengthModifier::AsInt64:
        return ArgType(Ctx.LongLongTy, "__int64");
      case LengthModifier::AsIntMax:
        return ArgType(Ctx.getIntMaxType(), "intmax_t");
      case LengthModifier::AsSizeT:
        return ArgType::makeSizeT(ArgType(Ctx.getSignedSizeType(), "ssize_t"));
      case LengthModifier::AsInt3264:
        return Ctx.getTargetInfo().getTriple().isArch64Bit()
                   ? ArgType(Ctx.LongLongTy, "__int64")
                   : ArgType(Ctx.IntTy, "__int32");
      case LengthModifier::AsPtrDiff:
        return ArgType::makePtrdiffT(
            ArgType(Ctx.getPointerDiffType(), "ptrdiff_t"));
      case LengthModifier::AsAllocate:
      case LengthModifier::AsMAllocate:
      case LengthModifier::AsWide:
        return ArgType::Invalid();
    }

  if (CS.isUIntArg())
    switch (LM.getKind()) {
      case LengthModifier::AsLongDouble:
        // GNU extension.
        return Ctx.UnsignedLongLongTy;
      case LengthModifier::None:
        return Ctx.UnsignedIntTy;
      case LengthModifier::AsInt32:
        return ArgType(Ctx.UnsignedIntTy, "unsigned __int32");
      case LengthModifier::AsChar: return Ctx.UnsignedCharTy;
      case LengthModifier::AsShort: return Ctx.UnsignedShortTy;
      case LengthModifier::AsLong: return Ctx.UnsignedLongTy;
      case LengthModifier::AsLongLong:
      case LengthModifier::AsQuad:
        return Ctx.UnsignedLongLongTy;
      case LengthModifier::AsInt64:
        return ArgType(Ctx.UnsignedLongLongTy, "unsigned __int64");
      case LengthModifier::AsIntMax:
        return ArgType(Ctx.getUIntMaxType(), "uintmax_t");
      case LengthModifier::AsSizeT:
        return ArgType::makeSizeT(ArgType(Ctx.getSizeType(), "size_t"));
      case LengthModifier::AsInt3264:
        return Ctx.getTargetInfo().getTriple().isArch64Bit()
                   ? ArgType(Ctx.UnsignedLongLongTy, "unsigned __int64")
                   : ArgType(Ctx.UnsignedIntTy, "unsigned __int32");
      case LengthModifier::AsPtrDiff:
        return ArgType::makePtrdiffT(
            ArgType(Ctx.getUnsignedPointerDiffType(), "unsigned ptrdiff_t"));
      case LengthModifier::AsAllocate:
      case LengthModifier::AsMAllocate:
      case LengthModifier::AsWide:
        return ArgType::Invalid();
    }

  if (CS.isDoubleArg()) {
    if (LM.getKind() == LengthModifier::AsLongDouble)
      return Ctx.LongDoubleTy;
    return Ctx.DoubleTy;
  }

  if (CS.getKind() == ConversionSpecifier::nArg) {
    switch (LM.getKind()) {
      case LengthModifier::None:
        return ArgType::PtrTo(Ctx.IntTy);
      case LengthModifier::AsChar:
        return ArgType::PtrTo(Ctx.SignedCharTy);
      case LengthModifier::AsShort:
        return ArgType::PtrTo(Ctx.ShortTy);
      case LengthModifier::AsLong:
        return ArgType::PtrTo(Ctx.LongTy);
      case LengthModifier::AsLongLong:
      case LengthModifier::AsQuad:
        return ArgType::PtrTo(Ctx.LongLongTy);
      case LengthModifier::AsIntMax:
        return ArgType::PtrTo(ArgType(Ctx.getIntMaxType(), "intmax_t"));
      case LengthModifier::AsSizeT:
        return ArgType::PtrTo(ArgType(Ctx.getSignedSizeType(), "ssize_t"));
      case LengthModifier::AsPtrDiff:
        return ArgType::PtrTo(ArgType(Ctx.getPointerDiffType(), "ptrdiff_t"));
      case LengthModifier::AsLongDouble:
        return ArgType(); // FIXME: Is this a known extension?
      case LengthModifier::AsAllocate:
      case LengthModifier::AsMAllocate:
      case LengthModifier::AsInt32:
      case LengthModifier::AsInt3264:
      case LengthModifier::AsInt64:
      case LengthModifier::AsWide:
        return ArgType::Invalid();
    }
  }

  switch (CS.getKind()) {
    case ConversionSpecifier::sArg:
      if (LM.getKind() == LengthModifier::AsWideChar) {
        if (IsObjCLiteral)
          return ArgType(Ctx.getPointerType(Ctx.UnsignedShortTy.withConst()),
                         "const unichar *");
        return ArgType(ArgType::WCStrTy, "wchar_t *");
      }
      if (LM.getKind() == LengthModifier::AsWide)
        return ArgType(ArgType::WCStrTy, "wchar_t *");
      return ArgType::CStrTy;
    case ConversionSpecifier::SArg:
      if (IsObjCLiteral)
        return ArgType(Ctx.getPointerType(Ctx.UnsignedShortTy.withConst()),
                       "const unichar *");
      if (Ctx.getTargetInfo().getTriple().isOSMSVCRT() &&
          LM.getKind() == LengthModifier::AsShort)
        return ArgType::CStrTy;
      return ArgType(ArgType::WCStrTy, "wchar_t *");
    case ConversionSpecifier::CArg:
      if (IsObjCLiteral)
        return ArgType(Ctx.UnsignedShortTy, "unichar");
      if (Ctx.getTargetInfo().getTriple().isOSMSVCRT() &&
          LM.getKind() == LengthModifier::AsShort)
        return Ctx.IntTy;
      return ArgType(Ctx.WideCharTy, "wchar_t");
    case ConversionSpecifier::pArg:
    case ConversionSpecifier::PArg:
      return ArgType::CPointerTy;
    case ConversionSpecifier::ObjCObjArg:
      return ArgType::ObjCPointerTy;
    default:
      break;
  }

  // FIXME: Handle other cases.
  return ArgType();
}


ArgType PrintfSpecifier::getArgType(ASTContext &Ctx,
                                    bool IsObjCLiteral) const {
  const PrintfConversionSpecifier &CS = getConversionSpecifier();

  if (!CS.consumesDataArgument())
    return ArgType::Invalid();

  ArgType ScalarTy = getScalarArgType(Ctx, IsObjCLiteral);
  if (!ScalarTy.isValid() || VectorNumElts.isInvalid())
    return ScalarTy;

  return ScalarTy.makeVectorType(Ctx, VectorNumElts.getConstantAmount());
}

bool PrintfSpecifier::fixType(QualType QT, const LangOptions &LangOpt,
                              ASTContext &Ctx, bool IsObjCLiteral) {
  // %n is different from other conversion specifiers; don't try to fix it.
  if (CS.getKind() == ConversionSpecifier::nArg)
    return false;

  // Handle Objective-C objects first. Note that while the '%@' specifier will
  // not warn for structure pointer or void pointer arguments (because that's
  // how CoreFoundation objects are implemented), we only show a fixit for '%@'
  // if we know it's an object (block, id, class, or __attribute__((NSObject))).
  if (QT->isObjCRetainableType()) {
    if (!IsObjCLiteral)
      return false;

    CS.setKind(ConversionSpecifier::ObjCObjArg);

    // Disable irrelevant flags
    HasThousandsGrouping = false;
    HasPlusPrefix = false;
    HasSpacePrefix = false;
    HasAlternativeForm = false;
    HasLeadingZeroes = false;
    Precision.setHowSpecified(OptionalAmount::NotSpecified);
    LM.setKind(LengthModifier::None);

    return true;
  }

  // Handle strings next (char *, wchar_t *)
  if (QT->isPointerType() && (QT->getPointeeType()->isAnyCharacterType())) {
    CS.setKind(ConversionSpecifier::sArg);

    // Disable irrelevant flags
    HasAlternativeForm = 0;
    HasLeadingZeroes = 0;

    // Set the long length modifier for wide characters
    if (QT->getPointeeType()->isWideCharType())
      LM.setKind(LengthModifier::AsWideChar);
    else
      LM.setKind(LengthModifier::None);

    return true;
  }

  // If it's an enum, get its underlying type.
  if (const EnumType *ETy = QT->getAs<EnumType>())
    QT = ETy->getDecl()->getIntegerType();

  const BuiltinType *BT = QT->getAs<BuiltinType>();
  if (!BT) {
    const VectorType *VT = QT->getAs<VectorType>();
    if (VT) {
      QT = VT->getElementType();
      BT = QT->getAs<BuiltinType>();
      VectorNumElts = OptionalAmount(VT->getNumElements());
    }
  }

  // We can only work with builtin types.
  if (!BT)
    return false;

  // Set length modifier
  switch (BT->getKind()) {
  case BuiltinType::Bool:
  case BuiltinType::WChar_U:
  case BuiltinType::WChar_S:
  case BuiltinType::Char8: // FIXME: Treat like 'char'?
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::UInt128:
  case BuiltinType::Int128:
  case BuiltinType::Half:
  case BuiltinType::Float16:
  case BuiltinType::Float128:
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
    // Various types which are non-trivial to correct.
    return false;

#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
#define SIGNED_TYPE(Id, SingletonId)
#define UNSIGNED_TYPE(Id, SingletonId)
#define FLOATING_TYPE(Id, SingletonId)
#define BUILTIN_TYPE(Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
    // Misc other stuff which doesn't make sense here.
    return false;

  case BuiltinType::UInt:
  case BuiltinType::Int:
  case BuiltinType::Float:
  case BuiltinType::Double:
    LM.setKind(LengthModifier::None);
    break;

  case BuiltinType::Char_U:
  case BuiltinType::UChar:
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    LM.setKind(LengthModifier::AsChar);
    break;

  case BuiltinType::Short:
  case BuiltinType::UShort:
    LM.setKind(LengthModifier::AsShort);
    break;

  case BuiltinType::Long:
  case BuiltinType::ULong:
    LM.setKind(LengthModifier::AsLong);
    break;

  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    LM.setKind(LengthModifier::AsLongLong);
    break;

  case BuiltinType::LongDouble:
    LM.setKind(LengthModifier::AsLongDouble);
    break;
  }

  // Handle size_t, ptrdiff_t, etc. that have dedicated length modifiers in C99.
  if (isa<TypedefType>(QT) && (LangOpt.C99 || LangOpt.CPlusPlus11))
    namedTypeToLengthModifier(QT, LM);

  // If fixing the length modifier was enough, we might be done.
  if (hasValidLengthModifier(Ctx.getTargetInfo())) {
    // If we're going to offer a fix anyway, make sure the sign matches.
    switch (CS.getKind()) {
    case ConversionSpecifier::uArg:
    case ConversionSpecifier::UArg:
      if (QT->isSignedIntegerType())
        CS.setKind(clang::analyze_format_string::ConversionSpecifier::dArg);
      break;
    case ConversionSpecifier::dArg:
    case ConversionSpecifier::DArg:
    case ConversionSpecifier::iArg:
      if (QT->isUnsignedIntegerType() && !HasPlusPrefix)
        CS.setKind(clang::analyze_format_string::ConversionSpecifier::uArg);
      break;
    default:
      // Other specifiers do not have signed/unsigned variants.
      break;
    }

    const analyze_printf::ArgType &ATR = getArgType(Ctx, IsObjCLiteral);
    if (ATR.isValid() && ATR.matchesType(Ctx, QT))
      return true;
  }

  // Set conversion specifier and disable any flags which do not apply to it.
  // Let typedefs to char fall through to int, as %c is silly for uint8_t.
  if (!isa<TypedefType>(QT) && QT->isCharType()) {
    CS.setKind(ConversionSpecifier::cArg);
    LM.setKind(LengthModifier::None);
    Precision.setHowSpecified(OptionalAmount::NotSpecified);
    HasAlternativeForm = 0;
    HasLeadingZeroes = 0;
    HasPlusPrefix = 0;
  }
  // Test for Floating type first as LongDouble can pass isUnsignedIntegerType
  else if (QT->isRealFloatingType()) {
    CS.setKind(ConversionSpecifier::fArg);
  }
  else if (QT->isSignedIntegerType()) {
    CS.setKind(ConversionSpecifier::dArg);
    HasAlternativeForm = 0;
  }
  else if (QT->isUnsignedIntegerType()) {
    CS.setKind(ConversionSpecifier::uArg);
    HasAlternativeForm = 0;
    HasPlusPrefix = 0;
  } else {
    llvm_unreachable("Unexpected type");
  }

  return true;
}

void PrintfSpecifier::toString(raw_ostream &os) const {
  // Whilst some features have no defined order, we are using the order
  // appearing in the C99 standard (ISO/IEC 9899:1999 (E) 7.19.6.1)
  os << "%";

  // Positional args
  if (usesPositionalArg()) {
    os << getPositionalArgIndex() << "$";
  }

  // Conversion flags
  if (IsLeftJustified)    os << "-";
  if (HasPlusPrefix)      os << "+";
  if (HasSpacePrefix)     os << " ";
  if (HasAlternativeForm) os << "#";
  if (HasLeadingZeroes)   os << "0";

  // Minimum field width
  FieldWidth.toString(os);
  // Precision
  Precision.toString(os);

  // Vector modifier
  if (!VectorNumElts.isInvalid())
    os << 'v' << VectorNumElts.getConstantAmount();

  // Length modifier
  os << LM.toString();
  // Conversion specifier
  os << CS.toString();
}

bool PrintfSpecifier::hasValidPlusPrefix() const {
  if (!HasPlusPrefix)
    return true;

  // The plus prefix only makes sense for signed conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::DArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::FreeBSDrArg:
  case ConversionSpecifier::FreeBSDyArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidAlternativeForm() const {
  if (!HasAlternativeForm)
    return true;

  // Alternate form flag only valid with the oxXaAeEfFgG conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::OArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::FreeBSDrArg:
  case ConversionSpecifier::FreeBSDyArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidLeadingZeros() const {
  if (!HasLeadingZeroes)
    return true;

  // Leading zeroes flag only valid with the diouxXaAeEfFgG conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::DArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::OArg:
  case ConversionSpecifier::uArg:
  case ConversionSpecifier::UArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::FreeBSDrArg:
  case ConversionSpecifier::FreeBSDyArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidSpacePrefix() const {
  if (!HasSpacePrefix)
    return true;

  // The space prefix only makes sense for signed conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::DArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::FreeBSDrArg:
  case ConversionSpecifier::FreeBSDyArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidLeftJustified() const {
  if (!IsLeftJustified)
    return true;

  // The left justified flag is valid for all conversions except n
  switch (CS.getKind()) {
  case ConversionSpecifier::nArg:
    return false;

  default:
    return true;
  }
}

bool PrintfSpecifier::hasValidThousandsGroupingPrefix() const {
  if (!HasThousandsGrouping)
    return true;

  switch (CS.getKind()) {
    case ConversionSpecifier::dArg:
    case ConversionSpecifier::DArg:
    case ConversionSpecifier::iArg:
    case ConversionSpecifier::uArg:
    case ConversionSpecifier::UArg:
    case ConversionSpecifier::fArg:
    case ConversionSpecifier::FArg:
    case ConversionSpecifier::gArg:
    case ConversionSpecifier::GArg:
      return true;
    default:
      return false;
  }
}

bool PrintfSpecifier::hasValidPrecision() const {
  if (Precision.getHowSpecified() == OptionalAmount::NotSpecified)
    return true;

  // Precision is only valid with the diouxXaAeEfFgGsP conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::DArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::OArg:
  case ConversionSpecifier::uArg:
  case ConversionSpecifier::UArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::sArg:
  case ConversionSpecifier::FreeBSDrArg:
  case ConversionSpecifier::FreeBSDyArg:
  case ConversionSpecifier::PArg:
    return true;

  default:
    return false;
  }
}
bool PrintfSpecifier::hasValidFieldWidth() const {
  if (FieldWidth.getHowSpecified() == OptionalAmount::NotSpecified)
      return true;

  // The field width is valid for all conversions except n
  switch (CS.getKind()) {
  case ConversionSpecifier::nArg:
    return false;

  default:
    return true;
  }
}
