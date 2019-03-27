class AttrSubject;

class Stmt<bit abstract = 0> : AttrSubject {
  bit Abstract = abstract;
}

class DStmt<Stmt base, bit abstract = 0> : Stmt<abstract> {
  Stmt Base = base;
}

// Statements
def NullStmt : Stmt;
def CompoundStmt : Stmt;
def LabelStmt : Stmt;
def AttributedStmt : Stmt;
def IfStmt : Stmt;
def SwitchStmt : Stmt;
def WhileStmt : Stmt;
def DoStmt : Stmt;
def ForStmt : Stmt;
def GotoStmt : Stmt;
def IndirectGotoStmt : Stmt;
def ContinueStmt : Stmt;
def BreakStmt : Stmt;
def ReturnStmt : Stmt;
def DeclStmt  : Stmt;
def SwitchCase : Stmt<1>;
def CaseStmt : DStmt<SwitchCase>;
def DefaultStmt : DStmt<SwitchCase>;
def CapturedStmt : Stmt;

// Asm statements
def AsmStmt : Stmt<1>;
def GCCAsmStmt : DStmt<AsmStmt>;
def MSAsmStmt : DStmt<AsmStmt>;

// Obj-C statements
def ObjCAtTryStmt : Stmt;
def ObjCAtCatchStmt : Stmt;
def ObjCAtFinallyStmt : Stmt;
def ObjCAtThrowStmt : Stmt;
def ObjCAtSynchronizedStmt : Stmt;
def ObjCForCollectionStmt : Stmt;
def ObjCAutoreleasePoolStmt : Stmt;

// C++ statements
def CXXCatchStmt : Stmt;
def CXXTryStmt : Stmt;
def CXXForRangeStmt : Stmt;

// C++ Coroutines TS statements
def CoroutineBodyStmt : Stmt;
def CoreturnStmt : Stmt;

// Expressions
def Expr : Stmt<1>;
def PredefinedExpr : DStmt<Expr>;
def DeclRefExpr : DStmt<Expr>;
def IntegerLiteral : DStmt<Expr>;
def FixedPointLiteral : DStmt<Expr>;
def FloatingLiteral : DStmt<Expr>;
def ImaginaryLiteral : DStmt<Expr>;
def StringLiteral : DStmt<Expr>;
def CharacterLiteral : DStmt<Expr>;
def ParenExpr : DStmt<Expr>;
def UnaryOperator : DStmt<Expr>;
def OffsetOfExpr : DStmt<Expr>;
def UnaryExprOrTypeTraitExpr : DStmt<Expr>;
def ArraySubscriptExpr : DStmt<Expr>;
def OMPArraySectionExpr : DStmt<Expr>;
def CallExpr : DStmt<Expr>;
def MemberExpr : DStmt<Expr>;
def CastExpr : DStmt<Expr, 1>;
def BinaryOperator : DStmt<Expr>;
def CompoundAssignOperator : DStmt<BinaryOperator>;
def AbstractConditionalOperator : DStmt<Expr, 1>;
def ConditionalOperator : DStmt<AbstractConditionalOperator>;
def BinaryConditionalOperator : DStmt<AbstractConditionalOperator>;
def ImplicitCastExpr : DStmt<CastExpr>;
def ExplicitCastExpr : DStmt<CastExpr, 1>;
def CStyleCastExpr : DStmt<ExplicitCastExpr>;
def CompoundLiteralExpr : DStmt<Expr>;
def ExtVectorElementExpr : DStmt<Expr>;
def InitListExpr : DStmt<Expr>;
def DesignatedInitExpr : DStmt<Expr>;
def DesignatedInitUpdateExpr : DStmt<Expr>;
def ImplicitValueInitExpr : DStmt<Expr>;
def NoInitExpr : DStmt<Expr>;
def ArrayInitLoopExpr : DStmt<Expr>;
def ArrayInitIndexExpr : DStmt<Expr>;
def ParenListExpr : DStmt<Expr>;
def VAArgExpr : DStmt<Expr>;
def GenericSelectionExpr : DStmt<Expr>;
def PseudoObjectExpr : DStmt<Expr>;

// Wrapper expressions
def FullExpr : DStmt<Expr, 1>;
def ConstantExpr : DStmt<FullExpr>;

// Atomic expressions
def AtomicExpr : DStmt<Expr>;

// GNU Extensions.
def AddrLabelExpr : DStmt<Expr>;
def StmtExpr : DStmt<Expr>;
def ChooseExpr : DStmt<Expr>;
def GNUNullExpr : DStmt<Expr>;

// C++ Expressions.
def CXXOperatorCallExpr : DStmt<CallExpr>;
def CXXMemberCallExpr : DStmt<CallExpr>;
def CXXNamedCastExpr : DStmt<ExplicitCastExpr, 1>;
def CXXStaticCastExpr : DStmt<CXXNamedCastExpr>;
def CXXDynamicCastExpr : DStmt<CXXNamedCastExpr>;
def CXXReinterpretCastExpr : DStmt<CXXNamedCastExpr>;
def CXXConstCastExpr : DStmt<CXXNamedCastExpr>;
def CXXFunctionalCastExpr : DStmt<ExplicitCastExpr>;
def CXXTypeidExpr : DStmt<Expr>;
def UserDefinedLiteral : DStmt<CallExpr>;
def CXXBoolLiteralExpr : DStmt<Expr>;
def CXXNullPtrLiteralExpr : DStmt<Expr>;
def CXXThisExpr : DStmt<Expr>;
def CXXThrowExpr : DStmt<Expr>;
def CXXDefaultArgExpr : DStmt<Expr>;
def CXXDefaultInitExpr : DStmt<Expr>;
def CXXScalarValueInitExpr : DStmt<Expr>;
def CXXStdInitializerListExpr : DStmt<Expr>;
def CXXNewExpr : DStmt<Expr>;
def CXXDeleteExpr : DStmt<Expr>;
def CXXPseudoDestructorExpr : DStmt<Expr>;
def TypeTraitExpr : DStmt<Expr>;
def ArrayTypeTraitExpr : DStmt<Expr>;
def ExpressionTraitExpr : DStmt<Expr>;
def DependentScopeDeclRefExpr : DStmt<Expr>;
def CXXConstructExpr : DStmt<Expr>;
def CXXInheritedCtorInitExpr : DStmt<Expr>;
def CXXBindTemporaryExpr : DStmt<Expr>;
def ExprWithCleanups : DStmt<FullExpr>;
def CXXTemporaryObjectExpr : DStmt<CXXConstructExpr>;
def CXXUnresolvedConstructExpr : DStmt<Expr>;
def CXXDependentScopeMemberExpr : DStmt<Expr>;
def OverloadExpr : DStmt<Expr, 1>;
def UnresolvedLookupExpr : DStmt<OverloadExpr>;
def UnresolvedMemberExpr : DStmt<OverloadExpr>;
def CXXNoexceptExpr : DStmt<Expr>;
def PackExpansionExpr : DStmt<Expr>;
def SizeOfPackExpr : DStmt<Expr>;
def SubstNonTypeTemplateParmExpr : DStmt<Expr>;
def SubstNonTypeTemplateParmPackExpr : DStmt<Expr>;
def FunctionParmPackExpr : DStmt<Expr>;
def MaterializeTemporaryExpr : DStmt<Expr>;
def LambdaExpr : DStmt<Expr>;
def CXXFoldExpr : DStmt<Expr>;

// C++ Coroutines TS expressions
def CoroutineSuspendExpr : DStmt<Expr, 1>;
def CoawaitExpr : DStmt<CoroutineSuspendExpr>;
def DependentCoawaitExpr : DStmt<Expr>;
def CoyieldExpr : DStmt<CoroutineSuspendExpr>;

// Obj-C Expressions.
def ObjCStringLiteral : DStmt<Expr>;
def ObjCBoxedExpr : DStmt<Expr>;
def ObjCArrayLiteral : DStmt<Expr>;
def ObjCDictionaryLiteral : DStmt<Expr>;
def ObjCEncodeExpr : DStmt<Expr>;
def ObjCMessageExpr : DStmt<Expr>;
def ObjCSelectorExpr : DStmt<Expr>;
def ObjCProtocolExpr : DStmt<Expr>;
def ObjCIvarRefExpr : DStmt<Expr>;
def ObjCPropertyRefExpr : DStmt<Expr>;
def ObjCIsaExpr : DStmt<Expr>;
def ObjCIndirectCopyRestoreExpr : DStmt<Expr>;
def ObjCBoolLiteralExpr : DStmt<Expr>;
def ObjCSubscriptRefExpr : DStmt<Expr>;
def ObjCAvailabilityCheckExpr : DStmt<Expr>;

// Obj-C ARC Expressions.
def ObjCBridgedCastExpr : DStmt<ExplicitCastExpr>;

// CUDA Expressions.
def CUDAKernelCallExpr : DStmt<CallExpr>;

// Clang Extensions.
def ShuffleVectorExpr : DStmt<Expr>;
def ConvertVectorExpr : DStmt<Expr>;
def BlockExpr : DStmt<Expr>;
def OpaqueValueExpr : DStmt<Expr>;
def TypoExpr : DStmt<Expr>;

// Microsoft Extensions.
def MSPropertyRefExpr : DStmt<Expr>;
def MSPropertySubscriptExpr : DStmt<Expr>;
def CXXUuidofExpr : DStmt<Expr>;
def SEHTryStmt : Stmt;
def SEHExceptStmt : Stmt;
def SEHFinallyStmt : Stmt;
def SEHLeaveStmt : Stmt;
def MSDependentExistsStmt : Stmt;

// OpenCL Extensions.
def AsTypeExpr : DStmt<Expr>;

// OpenMP Directives.
def OMPExecutableDirective : Stmt<1>;
def OMPLoopDirective : DStmt<OMPExecutableDirective, 1>;
def OMPParallelDirective : DStmt<OMPExecutableDirective>;
def OMPSimdDirective : DStmt<OMPLoopDirective>;
def OMPForDirective : DStmt<OMPLoopDirective>;
def OMPForSimdDirective : DStmt<OMPLoopDirective>;
def OMPSectionsDirective : DStmt<OMPExecutableDirective>;
def OMPSectionDirective : DStmt<OMPExecutableDirective>;
def OMPSingleDirective : DStmt<OMPExecutableDirective>;
def OMPMasterDirective : DStmt<OMPExecutableDirective>;
def OMPCriticalDirective : DStmt<OMPExecutableDirective>;
def OMPParallelForDirective : DStmt<OMPLoopDirective>;
def OMPParallelForSimdDirective : DStmt<OMPLoopDirective>;
def OMPParallelSectionsDirective : DStmt<OMPExecutableDirective>;
def OMPTaskDirective : DStmt<OMPExecutableDirective>;
def OMPTaskyieldDirective : DStmt<OMPExecutableDirective>;
def OMPBarrierDirective : DStmt<OMPExecutableDirective>;
def OMPTaskwaitDirective : DStmt<OMPExecutableDirective>;
def OMPTaskgroupDirective : DStmt<OMPExecutableDirective>;
def OMPFlushDirective : DStmt<OMPExecutableDirective>;
def OMPOrderedDirective : DStmt<OMPExecutableDirective>;
def OMPAtomicDirective : DStmt<OMPExecutableDirective>;
def OMPTargetDirective : DStmt<OMPExecutableDirective>;
def OMPTargetDataDirective : DStmt<OMPExecutableDirective>;
def OMPTargetEnterDataDirective : DStmt<OMPExecutableDirective>;
def OMPTargetExitDataDirective : DStmt<OMPExecutableDirective>;
def OMPTargetParallelDirective : DStmt<OMPExecutableDirective>;
def OMPTargetParallelForDirective : DStmt<OMPExecutableDirective>;
def OMPTargetUpdateDirective : DStmt<OMPExecutableDirective>;
def OMPTeamsDirective : DStmt<OMPExecutableDirective>;
def OMPCancellationPointDirective : DStmt<OMPExecutableDirective>;
def OMPCancelDirective : DStmt<OMPExecutableDirective>;
def OMPTaskLoopDirective : DStmt<OMPLoopDirective>;
def OMPTaskLoopSimdDirective : DStmt<OMPLoopDirective>;
def OMPDistributeDirective : DStmt<OMPLoopDirective>;
def OMPDistributeParallelForDirective : DStmt<OMPLoopDirective>;
def OMPDistributeParallelForSimdDirective : DStmt<OMPLoopDirective>;
def OMPDistributeSimdDirective : DStmt<OMPLoopDirective>;
def OMPTargetParallelForSimdDirective : DStmt<OMPLoopDirective>;
def OMPTargetSimdDirective : DStmt<OMPLoopDirective>;
def OMPTeamsDistributeDirective : DStmt<OMPLoopDirective>;
def OMPTeamsDistributeSimdDirective : DStmt<OMPLoopDirective>;
def OMPTeamsDistributeParallelForSimdDirective : DStmt<OMPLoopDirective>;
def OMPTeamsDistributeParallelForDirective : DStmt<OMPLoopDirective>;
def OMPTargetTeamsDirective : DStmt<OMPExecutableDirective>;
def OMPTargetTeamsDistributeDirective : DStmt<OMPLoopDirective>;
def OMPTargetTeamsDistributeParallelForDirective : DStmt<OMPLoopDirective>;
def OMPTargetTeamsDistributeParallelForSimdDirective : DStmt<OMPLoopDirective>;
def OMPTargetTeamsDistributeSimdDirective : DStmt<OMPLoopDirective>;
