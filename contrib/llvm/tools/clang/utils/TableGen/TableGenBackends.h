//===- TableGenBackends.h - Declarations for Clang TableGen Backends ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for all of the Clang TableGen
// backends. A "TableGen backend" is just a function. See
// "$LLVM_ROOT/utils/TableGen/TableGenBackends.h" for more info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_UTILS_TABLEGEN_TABLEGENBACKENDS_H
#define LLVM_CLANG_UTILS_TABLEGEN_TABLEGENBACKENDS_H

#include <string>

namespace llvm {
  class raw_ostream;
  class RecordKeeper;
}

namespace clang {

void EmitClangDeclContext(llvm::RecordKeeper &RK, llvm::raw_ostream &OS);
void EmitClangASTNodes(llvm::RecordKeeper &RK, llvm::raw_ostream &OS,
                       const std::string &N, const std::string &S);

void EmitClangAttrParserStringSwitches(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrSubjectMatchRulesParserStringSwitches(llvm::RecordKeeper &Records,
                                                        llvm::raw_ostream &OS);
void EmitClangAttrClass(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrImpl(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrList(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrSubjectMatchRuleList(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrPCHRead(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrPCHWrite(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrHasAttrImpl(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrSpellingListIndex(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrASTVisitor(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrTemplateInstantiate(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrList(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrImpl(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrKinds(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrTextNodeDump(llvm::RecordKeeper &Records,
                               llvm::raw_ostream &OS);
void EmitClangAttrNodeTraverse(llvm::RecordKeeper &Records,
                               llvm::raw_ostream &OS);

void EmitClangDiagsDefs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS,
                        const std::string &Component);
void EmitClangDiagGroups(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangDiagsIndexName(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangSACheckers(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangCommentHTMLTags(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangCommentHTMLTagsProperties(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangCommentHTMLNamedCharacterReferences(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangCommentCommandInfo(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangCommentCommandList(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitNeon(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitFP16(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonSema(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonTest(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeon2(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonSema2(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonTest2(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangAttrDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangDiagDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangOptDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangDataCollectors(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitTestPragmaAttributeSupportedAttributes(llvm::RecordKeeper &Records,
                                                llvm::raw_ostream &OS);

} // end namespace clang

#endif
