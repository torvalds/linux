//===----------------------- CodeRegionGenerator.cpp ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines classes responsible for generating llvm-mca
/// CodeRegions from various types of input. llvm-mca only analyzes CodeRegions,
/// so the classes here provide the input-to-CodeRegions translation.
//
//===----------------------------------------------------------------------===//

#include "CodeRegionGenerator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SMLoc.h"
#include <memory>

namespace llvm {
namespace mca {

// This virtual dtor serves as the anchor for the CodeRegionGenerator class.
CodeRegionGenerator::~CodeRegionGenerator() {}

// A comment consumer that parses strings.  The only valid tokens are strings.
class MCACommentConsumer : public AsmCommentConsumer {
public:
  CodeRegions &Regions;

  MCACommentConsumer(CodeRegions &R) : Regions(R) {}
  void HandleComment(SMLoc Loc, StringRef CommentText) override;
};

// This class provides the callbacks that occur when parsing input assembly.
class MCStreamerWrapper final : public MCStreamer {
  CodeRegions &Regions;

public:
  MCStreamerWrapper(MCContext &Context, mca::CodeRegions &R)
      : MCStreamer(Context), Regions(R) {}

  // We only want to intercept the emission of new instructions.
  virtual void EmitInstruction(const MCInst &Inst,
                               const MCSubtargetInfo & /* unused */,
                               bool /* unused */) override {
    Regions.addInstruction(Inst);
  }

  bool EmitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return true;
  }

  void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        unsigned ByteAlignment) override {}
  void EmitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, unsigned ByteAlignment = 0,
                    SMLoc Loc = SMLoc()) override {}
  void EmitGPRel32Value(const MCExpr *Value) override {}
  void BeginCOFFSymbolDef(const MCSymbol *Symbol) override {}
  void EmitCOFFSymbolStorageClass(int StorageClass) override {}
  void EmitCOFFSymbolType(int Type) override {}
  void EndCOFFSymbolDef() override {}

  ArrayRef<MCInst> GetInstructionSequence(unsigned Index) const {
    return Regions.getInstructionSequence(Index);
  }
};

void MCACommentConsumer::HandleComment(SMLoc Loc, StringRef CommentText) {
  // Skip empty comments.
  StringRef Comment(CommentText);
  if (Comment.empty())
    return;

  // Skip spaces and tabs.
  unsigned Position = Comment.find_first_not_of(" \t");
  if (Position >= Comment.size())
    // We reached the end of the comment. Bail out.
    return;

  Comment = Comment.drop_front(Position);
  if (Comment.consume_front("LLVM-MCA-END")) {
    Regions.endRegion(Loc);
    return;
  }

  // Try to parse the LLVM-MCA-BEGIN comment.
  if (!Comment.consume_front("LLVM-MCA-BEGIN"))
    return;

  // Skip spaces and tabs.
  Position = Comment.find_first_not_of(" \t");
  if (Position < Comment.size())
    Comment = Comment.drop_front(Position);
  // Use the rest of the string as a descriptor for this code snippet.
  Regions.beginRegion(Comment, Loc);
}

Expected<const CodeRegions &> AsmCodeRegionGenerator::parseCodeRegions() {
  MCTargetOptions Opts;
  Opts.PreserveAsmComments = false;
  MCStreamerWrapper Str(Ctx, Regions);

  // Create a MCAsmParser and setup the lexer to recognize llvm-mca ASM
  // comments.
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(Regions.getSourceMgr(), Ctx, Str, MAI));
  MCAsmLexer &Lexer = Parser->getLexer();
  MCACommentConsumer CC(Regions);
  Lexer.setCommentConsumer(&CC);

  // Create a target-specific parser and perform the parse.
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget.createMCAsmParser(STI, *Parser, MCII, Opts));
  if (!TAP)
    return make_error<StringError>(
        "This target does not support assembly parsing.",
        inconvertibleErrorCode());
  Parser->setTargetParser(*TAP);
  Parser->Run(false);

  // Get the assembler dialect from the input.  llvm-mca will use this as the
  // default dialect when printing reports.
  AssemblerDialect = Parser->getAssemblerDialect();
  return Regions;
}

} // namespace mca
} // namespace llvm
