//===-- ClangForward.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangForward_h_
#define liblldb_ClangForward_h_


#if defined(__cplusplus)

namespace clang {
namespace Builtin {
class Context;
}

class Action;
class ASTConsumer;
class ASTContext;
class ASTRecordLayout;
class AddrLabelExpr;
class AnalyzerOptions;
class BinaryOperator;
class ClassTemplateDecl;
class ClassTemplateSpecializationDecl;
class CodeGenOptions;
class CodeGenerator;
class CompilerInstance;
class CompoundStmt;
class CXXBaseSpecifier;
class CXXBoolLiteralExpr;
class CXXFunctionalCastExpr;
class CXXMethodDecl;
class CXXNamedCastExpr;
class CXXRecordDecl;
class CXXThisExpr;
class CharacterLiteral;
class CompoundAssignOperator;
class Decl;
class DeclarationName;
class DeclaratorDecl;
class DeclContext;
class DeclRefExpr;
class DeclStmt;
class DependencyOutputOptions;
class Diagnostic;
class DiagnosticConsumer;
class DiagnosticsEngine;
class DiagnosticOptions;
class EnumDecl;
class EnumConstantDecl;
class Expr;
class ExternalASTSource;
class ExtVectorElementExpr;
class FieldDecl;
class FileManager;
class FileSystemOptions;
class FloatingLiteral;
class FrontendOptions;
class FunctionDecl;
class FunctionTemplateDecl;
class FunctionTemplateSpecializationInfo;
class GotoStmt;
class HeaderSearchOptions;
class IdentifierInfo;
class IdentifierTable;
class IntegerLiteral;
class LabelStmt;
class LangOptions;
class MacroDirective;
class MemberExpr;
class Module;
class NamedDecl;
class NamespaceDecl;
class NonTypeTemplateParmDecl;
class ObjCEncodeExpr;
class ObjCImplicitSetterGetterRefExpr;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class ObjCIvarRefExpr;
class ObjCMessageExpr;
class ObjCMethodDecl;
class ObjCPropertyRefExpr;
class ObjCProtocolDecl;
class ObjCProtocolExpr;
class ObjCSelectorExpr;
class ObjCSuperExpr;
class ParenExpr;
class ParmVarDecl;
class PredefinedExpr;
class PreprocessorOptions;
class PreprocessorOutputOptions;
class QualType;
class QualifiedNameType;
class RecordDecl;
class SelectorTable;
class SizeOfAlignOfExpr;
class SourceLocation;
class SourceManager;
class Stmt;
class StmtIteratorBase;
class StringLiteral;
class TagDecl;
class TargetInfo;
class TargetOptions;
class TemplateArgument;
class TemplateDecl;
class TemplateParameterList;
class TemplateTemplateParmDecl;
class TemplateTypeParmDecl;
class TextDiagnosticBuffer;
class TranslationUnitDecl;
class Type;
class TypeDecl;
class TypedefDecl;
class TypesCompatibleExpr;
class UnaryOperator;
class ValueDecl;
class VarDecl;
struct PrintingPolicy;
}

namespace llvm {
class APInt;
class APSInt;
class LLVMContext;
class ExecutionEngine;
}

#endif // #if defined(__cplusplus)
#endif // liblldb_ClangForward_h_
