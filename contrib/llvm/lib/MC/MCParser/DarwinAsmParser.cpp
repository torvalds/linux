//===- DarwinAsmParser.cpp - Darwin (Mach-O) Assembly Parser --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

using namespace llvm;

namespace {

/// Implementation of directive handling which is shared across all
/// Darwin targets.
class DarwinAsmParser : public MCAsmParserExtension {
  template<bool (DarwinAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<DarwinAsmParser, HandlerMethod>);
    getParser().addDirectiveHandler(Directive, Handler);
  }

  bool parseSectionSwitch(StringRef Segment, StringRef Section,
                          unsigned TAA = 0, unsigned ImplicitAlign = 0,
                          unsigned StubSize = 0);

  SMLoc LastVersionDirective;

public:
  DarwinAsmParser() = default;

  void Initialize(MCAsmParser &Parser) override {
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(Parser);

    addDirectiveHandler<&DarwinAsmParser::parseDirectiveAltEntry>(".alt_entry");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveDesc>(".desc");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveIndirectSymbol>(
      ".indirect_symbol");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveLsym>(".lsym");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveSubsectionsViaSymbols>(
      ".subsections_via_symbols");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveDumpOrLoad>(".dump");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveDumpOrLoad>(".load");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveSection>(".section");
    addDirectiveHandler<&DarwinAsmParser::parseDirectivePushSection>(
      ".pushsection");
    addDirectiveHandler<&DarwinAsmParser::parseDirectivePopSection>(
      ".popsection");
    addDirectiveHandler<&DarwinAsmParser::parseDirectivePrevious>(".previous");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveSecureLogUnique>(
      ".secure_log_unique");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveSecureLogReset>(
      ".secure_log_reset");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveTBSS>(".tbss");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveZerofill>(".zerofill");

    addDirectiveHandler<&DarwinAsmParser::parseDirectiveDataRegion>(
      ".data_region");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveDataRegionEnd>(
      ".end_data_region");

    // Special section directives.
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveBss>(".bss");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveConst>(".const");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveConstData>(
      ".const_data");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveConstructor>(
      ".constructor");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveCString>(
      ".cstring");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveData>(".data");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveDestructor>(
      ".destructor");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveDyld>(".dyld");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveFVMLibInit0>(
      ".fvmlib_init0");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveFVMLibInit1>(
      ".fvmlib_init1");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveLazySymbolPointers>(
        ".lazy_symbol_pointer");
    addDirectiveHandler<&DarwinAsmParser::parseDirectiveLinkerOption>(
      ".linker_option");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveLiteral16>(
      ".literal16");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveLiteral4>(
      ".literal4");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveLiteral8>(
      ".literal8");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveModInitFunc>(
      ".mod_init_func");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveModTermFunc>(
      ".mod_term_func");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveNonLazySymbolPointers>(
        ".non_lazy_symbol_pointer");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveThreadLocalVariablePointers>(
        ".thread_local_variable_pointer");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCCatClsMeth>(
      ".objc_cat_cls_meth");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCCatInstMeth>(
      ".objc_cat_inst_meth");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCCategory>(
      ".objc_category");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCClass>(
      ".objc_class");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCClassNames>(
      ".objc_class_names");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCClassVars>(
      ".objc_class_vars");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCClsMeth>(
      ".objc_cls_meth");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCClsRefs>(
      ".objc_cls_refs");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCInstMeth>(
      ".objc_inst_meth");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveObjCInstanceVars>(
        ".objc_instance_vars");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCMessageRefs>(
      ".objc_message_refs");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCMetaClass>(
      ".objc_meta_class");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveObjCMethVarNames>(
        ".objc_meth_var_names");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveObjCMethVarTypes>(
        ".objc_meth_var_types");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCModuleInfo>(
      ".objc_module_info");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCProtocol>(
      ".objc_protocol");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveObjCSelectorStrs>(
        ".objc_selector_strs");
    addDirectiveHandler<
      &DarwinAsmParser::parseSectionDirectiveObjCStringObject>(
        ".objc_string_object");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveObjCSymbols>(
      ".objc_symbols");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectivePICSymbolStub>(
      ".picsymbol_stub");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveStaticConst>(
      ".static_const");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveStaticData>(
      ".static_data");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveSymbolStub>(
      ".symbol_stub");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveTData>(".tdata");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveText>(".text");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveThreadInitFunc>(
      ".thread_init_func");
    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveTLV>(".tlv");

    addDirectiveHandler<&DarwinAsmParser::parseSectionDirectiveIdent>(".ident");
    addDirectiveHandler<&DarwinAsmParser::parseWatchOSVersionMin>(
      ".watchos_version_min");
    addDirectiveHandler<&DarwinAsmParser::parseTvOSVersionMin>(
      ".tvos_version_min");
    addDirectiveHandler<&DarwinAsmParser::parseIOSVersionMin>(
      ".ios_version_min");
    addDirectiveHandler<&DarwinAsmParser::parseMacOSXVersionMin>(
      ".macosx_version_min");
    addDirectiveHandler<&DarwinAsmParser::parseBuildVersion>(".build_version");

    LastVersionDirective = SMLoc();
  }

  bool parseDirectiveAltEntry(StringRef, SMLoc);
  bool parseDirectiveDesc(StringRef, SMLoc);
  bool parseDirectiveIndirectSymbol(StringRef, SMLoc);
  bool parseDirectiveDumpOrLoad(StringRef, SMLoc);
  bool parseDirectiveLsym(StringRef, SMLoc);
  bool parseDirectiveLinkerOption(StringRef, SMLoc);
  bool parseDirectiveSection(StringRef, SMLoc);
  bool parseDirectivePushSection(StringRef, SMLoc);
  bool parseDirectivePopSection(StringRef, SMLoc);
  bool parseDirectivePrevious(StringRef, SMLoc);
  bool parseDirectiveSecureLogReset(StringRef, SMLoc);
  bool parseDirectiveSecureLogUnique(StringRef, SMLoc);
  bool parseDirectiveSubsectionsViaSymbols(StringRef, SMLoc);
  bool parseDirectiveTBSS(StringRef, SMLoc);
  bool parseDirectiveZerofill(StringRef, SMLoc);
  bool parseDirectiveDataRegion(StringRef, SMLoc);
  bool parseDirectiveDataRegionEnd(StringRef, SMLoc);

  // Named Section Directive
  bool parseSectionDirectiveBss(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__bss");
  }

  bool parseSectionDirectiveConst(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__const");
  }

  bool parseSectionDirectiveStaticConst(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__static_const");
  }

  bool parseSectionDirectiveCString(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__cstring",
                              MachO::S_CSTRING_LITERALS);
  }

  bool parseSectionDirectiveLiteral4(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__literal4",
                              MachO::S_4BYTE_LITERALS, 4);
  }

  bool parseSectionDirectiveLiteral8(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__literal8",
                              MachO::S_8BYTE_LITERALS, 8);
  }

  bool parseSectionDirectiveLiteral16(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__literal16",
                              MachO::S_16BYTE_LITERALS, 16);
  }

  bool parseSectionDirectiveConstructor(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__constructor");
  }

  bool parseSectionDirectiveDestructor(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__destructor");
  }

  bool parseSectionDirectiveFVMLibInit0(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__fvmlib_init0");
  }

  bool parseSectionDirectiveFVMLibInit1(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__fvmlib_init1");
  }

  bool parseSectionDirectiveSymbolStub(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__symbol_stub",
                              MachO::S_SYMBOL_STUBS |
                              MachO::S_ATTR_PURE_INSTRUCTIONS,
                              // FIXME: Different on PPC and ARM.
                              0, 16);
  }

  bool parseSectionDirectivePICSymbolStub(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT","__picsymbol_stub",
                              MachO::S_SYMBOL_STUBS |
                              MachO::S_ATTR_PURE_INSTRUCTIONS, 0, 26);
  }

  bool parseSectionDirectiveData(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__data");
  }

  bool parseSectionDirectiveStaticData(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__static_data");
  }

  bool parseSectionDirectiveNonLazySymbolPointers(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__nl_symbol_ptr",
                              MachO::S_NON_LAZY_SYMBOL_POINTERS, 4);
  }

  bool parseSectionDirectiveLazySymbolPointers(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__la_symbol_ptr",
                              MachO::S_LAZY_SYMBOL_POINTERS, 4);
  }

  bool parseSectionDirectiveThreadLocalVariablePointers(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__thread_ptr",
                              MachO::S_THREAD_LOCAL_VARIABLE_POINTERS, 4);
  }

  bool parseSectionDirectiveDyld(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__dyld");
  }

  bool parseSectionDirectiveModInitFunc(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__mod_init_func",
                              MachO::S_MOD_INIT_FUNC_POINTERS, 4);
  }

  bool parseSectionDirectiveModTermFunc(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__mod_term_func",
                              MachO::S_MOD_TERM_FUNC_POINTERS, 4);
  }

  bool parseSectionDirectiveConstData(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__const");
  }

  bool parseSectionDirectiveObjCClass(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__class",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCMetaClass(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__meta_class",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCCatClsMeth(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__cat_cls_meth",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCCatInstMeth(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__cat_inst_meth",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCProtocol(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__protocol",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCStringObject(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__string_object",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCClsMeth(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__cls_meth",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCInstMeth(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__inst_meth",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCClsRefs(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__cls_refs",
                              MachO::S_ATTR_NO_DEAD_STRIP |
                              MachO::S_LITERAL_POINTERS, 4);
  }

  bool parseSectionDirectiveObjCMessageRefs(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__message_refs",
                              MachO::S_ATTR_NO_DEAD_STRIP |
                              MachO::S_LITERAL_POINTERS, 4);
  }

  bool parseSectionDirectiveObjCSymbols(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__symbols",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCCategory(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__category",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCClassVars(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__class_vars",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCInstanceVars(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__instance_vars",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCModuleInfo(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__module_info",
                              MachO::S_ATTR_NO_DEAD_STRIP);
  }

  bool parseSectionDirectiveObjCClassNames(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__cstring",
                              MachO::S_CSTRING_LITERALS);
  }

  bool parseSectionDirectiveObjCMethVarTypes(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__cstring",
                              MachO::S_CSTRING_LITERALS);
  }

  bool parseSectionDirectiveObjCMethVarNames(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__cstring",
                              MachO::S_CSTRING_LITERALS);
  }

  bool parseSectionDirectiveObjCSelectorStrs(StringRef, SMLoc) {
    return parseSectionSwitch("__OBJC", "__selector_strs",
                              MachO::S_CSTRING_LITERALS);
  }

  bool parseSectionDirectiveTData(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__thread_data",
                              MachO::S_THREAD_LOCAL_REGULAR);
  }

  bool parseSectionDirectiveText(StringRef, SMLoc) {
    return parseSectionSwitch("__TEXT", "__text",
                              MachO::S_ATTR_PURE_INSTRUCTIONS);
  }

  bool parseSectionDirectiveTLV(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__thread_vars",
                              MachO::S_THREAD_LOCAL_VARIABLES);
  }

  bool parseSectionDirectiveIdent(StringRef, SMLoc) {
    // Darwin silently ignores the .ident directive.
    getParser().eatToEndOfStatement();
    return false;
  }

  bool parseSectionDirectiveThreadInitFunc(StringRef, SMLoc) {
    return parseSectionSwitch("__DATA", "__thread_init",
                         MachO::S_THREAD_LOCAL_INIT_FUNCTION_POINTERS);
  }

  bool parseWatchOSVersionMin(StringRef Directive, SMLoc Loc) {
    return parseVersionMin(Directive, Loc, MCVM_WatchOSVersionMin);
  }
  bool parseTvOSVersionMin(StringRef Directive, SMLoc Loc) {
    return parseVersionMin(Directive, Loc, MCVM_TvOSVersionMin);
  }
  bool parseIOSVersionMin(StringRef Directive, SMLoc Loc) {
    return parseVersionMin(Directive, Loc, MCVM_IOSVersionMin);
  }
  bool parseMacOSXVersionMin(StringRef Directive, SMLoc Loc) {
    return parseVersionMin(Directive, Loc, MCVM_OSXVersionMin);
  }

  bool parseBuildVersion(StringRef Directive, SMLoc Loc);
  bool parseVersionMin(StringRef Directive, SMLoc Loc, MCVersionMinType Type);
  bool parseMajorMinorVersionComponent(unsigned *Major, unsigned *Minor,
                                       const char *VersionName);
  bool parseOptionalTrailingVersionComponent(unsigned *Component,
                                             const char *ComponentName);
  bool parseVersion(unsigned *Major, unsigned *Minor, unsigned *Update);
  bool parseSDKVersion(VersionTuple &SDKVersion);
  void checkVersion(StringRef Directive, StringRef Arg, SMLoc Loc,
                    Triple::OSType ExpectedOS);
};

} // end anonymous namespace

bool DarwinAsmParser::parseSectionSwitch(StringRef Segment, StringRef Section,
                                         unsigned TAA, unsigned Align,
                                         unsigned StubSize) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in section switching directive");
  Lex();

  // FIXME: Arch specific.
  bool isText = TAA & MachO::S_ATTR_PURE_INSTRUCTIONS;
  getStreamer().SwitchSection(getContext().getMachOSection(
      Segment, Section, TAA, StubSize,
      isText ? SectionKind::getText() : SectionKind::getData()));

  // Set the implicit alignment, if any.
  //
  // FIXME: This isn't really what 'as' does; I think it just uses the implicit
  // alignment on the section (e.g., if one manually inserts bytes into the
  // section, then just issuing the section switch directive will not realign
  // the section. However, this is arguably more reasonable behavior, and there
  // is no good reason for someone to intentionally emit incorrectly sized
  // values into the implicitly aligned sections.
  if (Align)
    getStreamer().EmitValueToAlignment(Align);

  return false;
}

/// parseDirectiveAltEntry
///  ::= .alt_entry identifier
bool DarwinAsmParser::parseDirectiveAltEntry(StringRef, SMLoc) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  // Look up symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  if (Sym->isDefined())
    return TokError(".alt_entry must preceed symbol definition");

  if (!getStreamer().EmitSymbolAttribute(Sym, MCSA_AltEntry))
    return TokError("unable to emit symbol attribute");

  Lex();
  return false;
}

/// parseDirectiveDesc
///  ::= .desc identifier , expression
bool DarwinAsmParser::parseDirectiveDesc(StringRef, SMLoc) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  // Handle the identifier as the key symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in '.desc' directive");
  Lex();

  int64_t DescValue;
  if (getParser().parseAbsoluteExpression(DescValue))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.desc' directive");

  Lex();

  // Set the n_desc field of this Symbol to this DescValue
  getStreamer().EmitSymbolDesc(Sym, DescValue);

  return false;
}

/// parseDirectiveIndirectSymbol
///  ::= .indirect_symbol identifier
bool DarwinAsmParser::parseDirectiveIndirectSymbol(StringRef, SMLoc Loc) {
  const MCSectionMachO *Current = static_cast<const MCSectionMachO *>(
      getStreamer().getCurrentSectionOnly());
  MachO::SectionType SectionType = Current->getType();
  if (SectionType != MachO::S_NON_LAZY_SYMBOL_POINTERS &&
      SectionType != MachO::S_LAZY_SYMBOL_POINTERS &&
      SectionType != MachO::S_THREAD_LOCAL_VARIABLE_POINTERS &&
      SectionType != MachO::S_SYMBOL_STUBS)
    return Error(Loc, "indirect symbol not in a symbol pointer or stub "
                      "section");

  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in .indirect_symbol directive");

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  // Assembler local symbols don't make any sense here. Complain loudly.
  if (Sym->isTemporary())
    return TokError("non-local symbol required in directive");

  if (!getStreamer().EmitSymbolAttribute(Sym, MCSA_IndirectSymbol))
    return TokError("unable to emit indirect symbol attribute for: " + Name);

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.indirect_symbol' directive");

  Lex();

  return false;
}

/// parseDirectiveDumpOrLoad
///  ::= ( .dump | .load ) "filename"
bool DarwinAsmParser::parseDirectiveDumpOrLoad(StringRef Directive,
                                               SMLoc IDLoc) {
  bool IsDump = Directive == ".dump";
  if (getLexer().isNot(AsmToken::String))
    return TokError("expected string in '.dump' or '.load' directive");

  Lex();

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.dump' or '.load' directive");

  Lex();

  // FIXME: If/when .dump and .load are implemented they will be done in the
  // the assembly parser and not have any need for an MCStreamer API.
  if (IsDump)
    return Warning(IDLoc, "ignoring directive .dump for now");
  else
    return Warning(IDLoc, "ignoring directive .load for now");
}

/// ParseDirectiveLinkerOption
///  ::= .linker_option "string" ( , "string" )*
bool DarwinAsmParser::parseDirectiveLinkerOption(StringRef IDVal, SMLoc) {
  SmallVector<std::string, 4> Args;
  while (true) {
    if (getLexer().isNot(AsmToken::String))
      return TokError("expected string in '" + Twine(IDVal) + "' directive");

    std::string Data;
    if (getParser().parseEscapedString(Data))
      return true;

    Args.push_back(Data);

    if (getLexer().is(AsmToken::EndOfStatement))
      break;

    if (getLexer().isNot(AsmToken::Comma))
      return TokError("unexpected token in '" + Twine(IDVal) + "' directive");
    Lex();
  }

  getStreamer().EmitLinkerOptions(Args);
  return false;
}

/// parseDirectiveLsym
///  ::= .lsym identifier , expression
bool DarwinAsmParser::parseDirectiveLsym(StringRef, SMLoc) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  // Handle the identifier as the key symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in '.lsym' directive");
  Lex();

  const MCExpr *Value;
  if (getParser().parseExpression(Value))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.lsym' directive");

  Lex();

  // We don't currently support this directive.
  //
  // FIXME: Diagnostic location!
  (void) Sym;
  return TokError("directive '.lsym' is unsupported");
}

/// parseDirectiveSection:
///   ::= .section identifier (',' identifier)*
bool DarwinAsmParser::parseDirectiveSection(StringRef, SMLoc) {
  SMLoc Loc = getLexer().getLoc();

  StringRef SectionName;
  if (getParser().parseIdentifier(SectionName))
    return Error(Loc, "expected identifier after '.section' directive");

  // Verify there is a following comma.
  if (!getLexer().is(AsmToken::Comma))
    return TokError("unexpected token in '.section' directive");

  std::string SectionSpec = SectionName;
  SectionSpec += ",";

  // Add all the tokens until the end of the line, ParseSectionSpecifier will
  // handle this.
  StringRef EOL = getLexer().LexUntilEndOfStatement();
  SectionSpec.append(EOL.begin(), EOL.end());

  Lex();
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.section' directive");
  Lex();

  StringRef Segment, Section;
  unsigned StubSize;
  unsigned TAA;
  bool TAAParsed;
  std::string ErrorStr =
    MCSectionMachO::ParseSectionSpecifier(SectionSpec, Segment, Section,
                                          TAA, TAAParsed, StubSize);

  if (!ErrorStr.empty())
    return Error(Loc, ErrorStr);

  // Issue a warning if the target is not powerpc and Section is a *coal* section.
  Triple TT = getParser().getContext().getObjectFileInfo()->getTargetTriple();
  Triple::ArchType ArchTy = TT.getArch();

  if (ArchTy != Triple::ppc && ArchTy != Triple::ppc64) {
    StringRef NonCoalSection = StringSwitch<StringRef>(Section)
                                   .Case("__textcoal_nt", "__text")
                                   .Case("__const_coal", "__const")
                                   .Case("__datacoal_nt", "__data")
                                   .Default(Section);

    if (!Section.equals(NonCoalSection)) {
      StringRef SectionVal(Loc.getPointer());
      size_t B = SectionVal.find(',') + 1, E = SectionVal.find(',', B);
      SMLoc BLoc = SMLoc::getFromPointer(SectionVal.data() + B);
      SMLoc ELoc = SMLoc::getFromPointer(SectionVal.data() + E);
      getParser().Warning(Loc, "section \"" + Section + "\" is deprecated",
                          SMRange(BLoc, ELoc));
      getParser().Note(Loc, "change section name to \"" + NonCoalSection +
                       "\"", SMRange(BLoc, ELoc));
    }
  }

  // FIXME: Arch specific.
  bool isText = Segment == "__TEXT";  // FIXME: Hack.
  getStreamer().SwitchSection(getContext().getMachOSection(
      Segment, Section, TAA, StubSize,
      isText ? SectionKind::getText() : SectionKind::getData()));
  return false;
}

/// ParseDirectivePushSection:
///   ::= .pushsection identifier (',' identifier)*
bool DarwinAsmParser::parseDirectivePushSection(StringRef S, SMLoc Loc) {
  getStreamer().PushSection();

  if (parseDirectiveSection(S, Loc)) {
    getStreamer().PopSection();
    return true;
  }

  return false;
}

/// ParseDirectivePopSection:
///   ::= .popsection
bool DarwinAsmParser::parseDirectivePopSection(StringRef, SMLoc) {
  if (!getStreamer().PopSection())
    return TokError(".popsection without corresponding .pushsection");
  return false;
}

/// ParseDirectivePrevious:
///   ::= .previous
bool DarwinAsmParser::parseDirectivePrevious(StringRef DirName, SMLoc) {
  MCSectionSubPair PreviousSection = getStreamer().getPreviousSection();
  if (!PreviousSection.first)
    return TokError(".previous without corresponding .section");
  getStreamer().SwitchSection(PreviousSection.first, PreviousSection.second);
  return false;
}

/// ParseDirectiveSecureLogUnique
///  ::= .secure_log_unique ... message ...
bool DarwinAsmParser::parseDirectiveSecureLogUnique(StringRef, SMLoc IDLoc) {
  StringRef LogMessage = getParser().parseStringToEndOfStatement();
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.secure_log_unique' directive");

  if (getContext().getSecureLogUsed())
    return Error(IDLoc, ".secure_log_unique specified multiple times");

  // Get the secure log path.
  const char *SecureLogFile = getContext().getSecureLogFile();
  if (!SecureLogFile)
    return Error(IDLoc, ".secure_log_unique used but AS_SECURE_LOG_FILE "
                 "environment variable unset.");

  // Open the secure log file if we haven't already.
  raw_fd_ostream *OS = getContext().getSecureLog();
  if (!OS) {
    std::error_code EC;
    auto NewOS = llvm::make_unique<raw_fd_ostream>(
        StringRef(SecureLogFile), EC, sys::fs::F_Append | sys::fs::F_Text);
    if (EC)
       return Error(IDLoc, Twine("can't open secure log file: ") +
                               SecureLogFile + " (" + EC.message() + ")");
    OS = NewOS.get();
    getContext().setSecureLog(std::move(NewOS));
  }

  // Write the message.
  unsigned CurBuf = getSourceManager().FindBufferContainingLoc(IDLoc);
  *OS << getSourceManager().getBufferInfo(CurBuf).Buffer->getBufferIdentifier()
      << ":" << getSourceManager().FindLineNumber(IDLoc, CurBuf) << ":"
      << LogMessage + "\n";

  getContext().setSecureLogUsed(true);

  return false;
}

/// ParseDirectiveSecureLogReset
///  ::= .secure_log_reset
bool DarwinAsmParser::parseDirectiveSecureLogReset(StringRef, SMLoc IDLoc) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.secure_log_reset' directive");

  Lex();

  getContext().setSecureLogUsed(false);

  return false;
}

/// parseDirectiveSubsectionsViaSymbols
///  ::= .subsections_via_symbols
bool DarwinAsmParser::parseDirectiveSubsectionsViaSymbols(StringRef, SMLoc) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.subsections_via_symbols' directive");

  Lex();

  getStreamer().EmitAssemblerFlag(MCAF_SubsectionsViaSymbols);

  return false;
}

/// ParseDirectiveTBSS
///  ::= .tbss identifier, size, align
bool DarwinAsmParser::parseDirectiveTBSS(StringRef, SMLoc) {
  SMLoc IDLoc = getLexer().getLoc();
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  // Handle the identifier as the key symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in directive");
  Lex();

  int64_t Size;
  SMLoc SizeLoc = getLexer().getLoc();
  if (getParser().parseAbsoluteExpression(Size))
    return true;

  int64_t Pow2Alignment = 0;
  SMLoc Pow2AlignmentLoc;
  if (getLexer().is(AsmToken::Comma)) {
    Lex();
    Pow2AlignmentLoc = getLexer().getLoc();
    if (getParser().parseAbsoluteExpression(Pow2Alignment))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.tbss' directive");

  Lex();

  if (Size < 0)
    return Error(SizeLoc, "invalid '.tbss' directive size, can't be less than"
                 "zero");

  // FIXME: Diagnose overflow.
  if (Pow2Alignment < 0)
    return Error(Pow2AlignmentLoc, "invalid '.tbss' alignment, can't be less"
                 "than zero");

  if (!Sym->isUndefined())
    return Error(IDLoc, "invalid symbol redefinition");

  getStreamer().EmitTBSSSymbol(getContext().getMachOSection(
                                 "__DATA", "__thread_bss",
                                 MachO::S_THREAD_LOCAL_ZEROFILL,
                                 0, SectionKind::getThreadBSS()),
                               Sym, Size, 1 << Pow2Alignment);

  return false;
}

/// ParseDirectiveZerofill
///  ::= .zerofill segname , sectname [, identifier , size_expression [
///      , align_expression ]]
bool DarwinAsmParser::parseDirectiveZerofill(StringRef, SMLoc) {
  StringRef Segment;
  if (getParser().parseIdentifier(Segment))
    return TokError("expected segment name after '.zerofill' directive");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in directive");
  Lex();

  StringRef Section;
  SMLoc SectionLoc = getLexer().getLoc();
  if (getParser().parseIdentifier(Section))
    return TokError("expected section name after comma in '.zerofill' "
                    "directive");

  // If this is the end of the line all that was wanted was to create the
  // the section but with no symbol.
  if (getLexer().is(AsmToken::EndOfStatement)) {
    // Create the zerofill section but no symbol
    getStreamer().EmitZerofill(
        getContext().getMachOSection(Segment, Section, MachO::S_ZEROFILL, 0,
                                     SectionKind::getBSS()),
        /*Symbol=*/nullptr, /*Size=*/0, /*ByteAlignment=*/0, SectionLoc);
    return false;
  }

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in directive");
  Lex();

  SMLoc IDLoc = getLexer().getLoc();
  StringRef IDStr;
  if (getParser().parseIdentifier(IDStr))
    return TokError("expected identifier in directive");

  // handle the identifier as the key symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(IDStr);

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("unexpected token in directive");
  Lex();

  int64_t Size;
  SMLoc SizeLoc = getLexer().getLoc();
  if (getParser().parseAbsoluteExpression(Size))
    return true;

  int64_t Pow2Alignment = 0;
  SMLoc Pow2AlignmentLoc;
  if (getLexer().is(AsmToken::Comma)) {
    Lex();
    Pow2AlignmentLoc = getLexer().getLoc();
    if (getParser().parseAbsoluteExpression(Pow2Alignment))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.zerofill' directive");

  Lex();

  if (Size < 0)
    return Error(SizeLoc, "invalid '.zerofill' directive size, can't be less "
                 "than zero");

  // NOTE: The alignment in the directive is a power of 2 value, the assembler
  // may internally end up wanting an alignment in bytes.
  // FIXME: Diagnose overflow.
  if (Pow2Alignment < 0)
    return Error(Pow2AlignmentLoc, "invalid '.zerofill' directive alignment, "
                 "can't be less than zero");

  if (!Sym->isUndefined())
    return Error(IDLoc, "invalid symbol redefinition");

  // Create the zerofill Symbol with Size and Pow2Alignment
  //
  // FIXME: Arch specific.
  getStreamer().EmitZerofill(getContext().getMachOSection(
                               Segment, Section, MachO::S_ZEROFILL,
                               0, SectionKind::getBSS()),
                             Sym, Size, 1 << Pow2Alignment, SectionLoc);

  return false;
}

/// ParseDirectiveDataRegion
///  ::= .data_region [ ( jt8 | jt16 | jt32 ) ]
bool DarwinAsmParser::parseDirectiveDataRegion(StringRef, SMLoc) {
  if (getLexer().is(AsmToken::EndOfStatement)) {
    Lex();
    getStreamer().EmitDataRegion(MCDR_DataRegion);
    return false;
  }
  StringRef RegionType;
  SMLoc Loc = getParser().getTok().getLoc();
  if (getParser().parseIdentifier(RegionType))
    return TokError("expected region type after '.data_region' directive");
  int Kind = StringSwitch<int>(RegionType)
    .Case("jt8", MCDR_DataRegionJT8)
    .Case("jt16", MCDR_DataRegionJT16)
    .Case("jt32", MCDR_DataRegionJT32)
    .Default(-1);
  if (Kind == -1)
    return Error(Loc, "unknown region type in '.data_region' directive");
  Lex();

  getStreamer().EmitDataRegion((MCDataRegionType)Kind);
  return false;
}

/// ParseDirectiveDataRegionEnd
///  ::= .end_data_region
bool DarwinAsmParser::parseDirectiveDataRegionEnd(StringRef, SMLoc) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in '.end_data_region' directive");

  Lex();
  getStreamer().EmitDataRegion(MCDR_DataRegionEnd);
  return false;
}

static bool isSDKVersionToken(const AsmToken &Tok) {
  return Tok.is(AsmToken::Identifier) && Tok.getIdentifier() == "sdk_version";
}

/// parseMajorMinorVersionComponent ::= major, minor
bool DarwinAsmParser::parseMajorMinorVersionComponent(unsigned *Major,
                                                      unsigned *Minor,
                                                      const char *VersionName) {
  // Get the major version number.
  if (getLexer().isNot(AsmToken::Integer))
    return TokError(Twine("invalid ") + VersionName +
                    " major version number, integer expected");
  int64_t MajorVal = getLexer().getTok().getIntVal();
  if (MajorVal > 65535 || MajorVal <= 0)
    return TokError(Twine("invalid ") + VersionName + " major version number");
  *Major = (unsigned)MajorVal;
  Lex();
  if (getLexer().isNot(AsmToken::Comma))
    return TokError(Twine(VersionName) +
                    " minor version number required, comma expected");
  Lex();
  // Get the minor version number.
  if (getLexer().isNot(AsmToken::Integer))
    return TokError(Twine("invalid ") + VersionName +
                    " minor version number, integer expected");
  int64_t MinorVal = getLexer().getTok().getIntVal();
  if (MinorVal > 255 || MinorVal < 0)
    return TokError(Twine("invalid ") + VersionName + " minor version number");
  *Minor = MinorVal;
  Lex();
  return false;
}

/// parseOptionalTrailingVersionComponent ::= , version_number
bool DarwinAsmParser::parseOptionalTrailingVersionComponent(
    unsigned *Component, const char *ComponentName) {
  assert(getLexer().is(AsmToken::Comma) && "comma expected");
  Lex();
  if (getLexer().isNot(AsmToken::Integer))
    return TokError(Twine("invalid ") + ComponentName +
                    " version number, integer expected");
  int64_t Val = getLexer().getTok().getIntVal();
  if (Val > 255 || Val < 0)
    return TokError(Twine("invalid ") + ComponentName + " version number");
  *Component = Val;
  Lex();
  return false;
}

/// parseVersion ::= parseMajorMinorVersionComponent
///                      parseOptionalTrailingVersionComponent
bool DarwinAsmParser::parseVersion(unsigned *Major, unsigned *Minor,
                                   unsigned *Update) {
  if (parseMajorMinorVersionComponent(Major, Minor, "OS"))
    return true;

  // Get the update level, if specified
  *Update = 0;
  if (getLexer().is(AsmToken::EndOfStatement) ||
      isSDKVersionToken(getLexer().getTok()))
    return false;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("invalid OS update specifier, comma expected");
  if (parseOptionalTrailingVersionComponent(Update, "OS update"))
    return true;
  return false;
}

bool DarwinAsmParser::parseSDKVersion(VersionTuple &SDKVersion) {
  assert(isSDKVersionToken(getLexer().getTok()) && "expected sdk_version");
  Lex();
  unsigned Major, Minor;
  if (parseMajorMinorVersionComponent(&Major, &Minor, "SDK"))
    return true;
  SDKVersion = VersionTuple(Major, Minor);

  // Get the subminor version, if specified.
  if (getLexer().is(AsmToken::Comma)) {
    unsigned Subminor;
    if (parseOptionalTrailingVersionComponent(&Subminor, "SDK subminor"))
      return true;
    SDKVersion = VersionTuple(Major, Minor, Subminor);
  }
  return false;
}

void DarwinAsmParser::checkVersion(StringRef Directive, StringRef Arg,
                                   SMLoc Loc, Triple::OSType ExpectedOS) {
  const Triple &Target = getContext().getObjectFileInfo()->getTargetTriple();
  if (Target.getOS() != ExpectedOS)
    Warning(Loc, Twine(Directive) +
            (Arg.empty() ? Twine() : Twine(' ') + Arg) +
            " used while targeting " + Target.getOSName());

  if (LastVersionDirective.isValid()) {
    Warning(Loc, "overriding previous version directive");
    Note(LastVersionDirective, "previous definition is here");
  }
  LastVersionDirective = Loc;
}

static Triple::OSType getOSTypeFromMCVM(MCVersionMinType Type) {
  switch (Type) {
  case MCVM_WatchOSVersionMin: return Triple::WatchOS;
  case MCVM_TvOSVersionMin:    return Triple::TvOS;
  case MCVM_IOSVersionMin:     return Triple::IOS;
  case MCVM_OSXVersionMin:     return Triple::MacOSX;
  }
  llvm_unreachable("Invalid mc version min type");
}

/// parseVersionMin
///   ::= .ios_version_min parseVersion parseSDKVersion
///   |   .macosx_version_min parseVersion parseSDKVersion
///   |   .tvos_version_min parseVersion parseSDKVersion
///   |   .watchos_version_min parseVersion parseSDKVersion
bool DarwinAsmParser::parseVersionMin(StringRef Directive, SMLoc Loc,
                                      MCVersionMinType Type) {
  unsigned Major;
  unsigned Minor;
  unsigned Update;
  if (parseVersion(&Major, &Minor, &Update))
    return true;

  VersionTuple SDKVersion;
  if (isSDKVersionToken(getLexer().getTok()) && parseSDKVersion(SDKVersion))
    return true;

  if (parseToken(AsmToken::EndOfStatement))
    return addErrorSuffix(Twine(" in '") + Directive + "' directive");

  Triple::OSType ExpectedOS = getOSTypeFromMCVM(Type);
  checkVersion(Directive, StringRef(), Loc, ExpectedOS);
  getStreamer().EmitVersionMin(Type, Major, Minor, Update, SDKVersion);
  return false;
}

static Triple::OSType getOSTypeFromPlatform(MachO::PlatformType Type) {
  switch (Type) {
  case MachO::PLATFORM_MACOS:   return Triple::MacOSX;
  case MachO::PLATFORM_IOS:     return Triple::IOS;
  case MachO::PLATFORM_TVOS:    return Triple::TvOS;
  case MachO::PLATFORM_WATCHOS: return Triple::WatchOS;
  case MachO::PLATFORM_BRIDGEOS:         /* silence warning */ break;
  case MachO::PLATFORM_IOSSIMULATOR:     /* silence warning */ break;
  case MachO::PLATFORM_TVOSSIMULATOR:    /* silence warning */ break;
  case MachO::PLATFORM_WATCHOSSIMULATOR: /* silence warning */ break;
  }
  llvm_unreachable("Invalid mach-o platform type");
}

/// parseBuildVersion
///   ::= .build_version (macos|ios|tvos|watchos), parseVersion parseSDKVersion
bool DarwinAsmParser::parseBuildVersion(StringRef Directive, SMLoc Loc) {
  StringRef PlatformName;
  SMLoc PlatformLoc = getTok().getLoc();
  if (getParser().parseIdentifier(PlatformName))
    return TokError("platform name expected");

  unsigned Platform = StringSwitch<unsigned>(PlatformName)
    .Case("macos", MachO::PLATFORM_MACOS)
    .Case("ios", MachO::PLATFORM_IOS)
    .Case("tvos", MachO::PLATFORM_TVOS)
    .Case("watchos", MachO::PLATFORM_WATCHOS)
    .Default(0);
  if (Platform == 0)
    return Error(PlatformLoc, "unknown platform name");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("version number required, comma expected");
  Lex();

  unsigned Major;
  unsigned Minor;
  unsigned Update;
  if (parseVersion(&Major, &Minor, &Update))
    return true;

  VersionTuple SDKVersion;
  if (isSDKVersionToken(getLexer().getTok()) && parseSDKVersion(SDKVersion))
    return true;

  if (parseToken(AsmToken::EndOfStatement))
    return addErrorSuffix(" in '.build_version' directive");

  Triple::OSType ExpectedOS
    = getOSTypeFromPlatform((MachO::PlatformType)Platform);
  checkVersion(Directive, PlatformName, Loc, ExpectedOS);
  getStreamer().EmitBuildVersion(Platform, Major, Minor, Update, SDKVersion);
  return false;
}


namespace llvm {

MCAsmParserExtension *createDarwinAsmParser() {
  return new DarwinAsmParser;
}

} // end llvm namespace
