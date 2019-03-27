//==--- DiagnosticGroups.td - Diagnostic Group Definitions ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def ImplicitFunctionDeclare : DiagGroup<"implicit-function-declaration">;
def ImplicitInt : DiagGroup<"implicit-int">;

// Aggregation warning settings.
def Implicit : DiagGroup<"implicit", [
    ImplicitFunctionDeclare,
    ImplicitInt
]>;

// Empty DiagGroups are recognized by clang but ignored.
def : DiagGroup<"abi">;
def AbsoluteValue : DiagGroup<"absolute-value">;
def AddressOfTemporary : DiagGroup<"address-of-temporary">;
def : DiagGroup<"aggregate-return">;
def GNUAlignofExpression : DiagGroup<"gnu-alignof-expression">;
def AmbigMemberTemplate : DiagGroup<"ambiguous-member-template">;
def GNUAnonymousStruct : DiagGroup<"gnu-anonymous-struct">;
def GNUAutoType : DiagGroup<"gnu-auto-type">;
def ArrayBounds : DiagGroup<"array-bounds">;
def ArrayBoundsPointerArithmetic : DiagGroup<"array-bounds-pointer-arithmetic">;
def AutoDisableVptrSanitizer : DiagGroup<"auto-disable-vptr-sanitizer">;
def Availability : DiagGroup<"availability">;
def Section : DiagGroup<"section">;
def AutoImport : DiagGroup<"auto-import">;
def FrameworkHdrQuotedInclude : DiagGroup<"quoted-include-in-framework-header">;
def FrameworkIncludePrivateFromPublic :
  DiagGroup<"framework-include-private-from-public">;
def FrameworkHdrAtImport : DiagGroup<"atimport-in-framework-header">;
def CXX14BinaryLiteral : DiagGroup<"c++14-binary-literal">;
def CXXPre14CompatBinaryLiteral : DiagGroup<"c++98-c++11-compat-binary-literal">;
def GNUBinaryLiteral : DiagGroup<"gnu-binary-literal">;
def BinaryLiteral : DiagGroup<"binary-literal", [CXX14BinaryLiteral,
                                                 CXXPre14CompatBinaryLiteral,
                                                 GNUBinaryLiteral]>;
def GNUCompoundLiteralInitializer : DiagGroup<"gnu-compound-literal-initializer">;
def BitFieldConstantConversion : DiagGroup<"bitfield-constant-conversion">;
def BitFieldEnumConversion : DiagGroup<"bitfield-enum-conversion">;
def BitFieldWidth : DiagGroup<"bitfield-width">;
def CoroutineMissingUnhandledException :
  DiagGroup<"coroutine-missing-unhandled-exception">;
def Coroutine : DiagGroup<"coroutine", [CoroutineMissingUnhandledException]>;
def ConstantConversion :
  DiagGroup<"constant-conversion", [ BitFieldConstantConversion ] >;
def LiteralConversion : DiagGroup<"literal-conversion">;
def StringConversion : DiagGroup<"string-conversion">;
def SignConversion : DiagGroup<"sign-conversion">;
def PointerBoolConversion : DiagGroup<"pointer-bool-conversion">;
def UndefinedBoolConversion : DiagGroup<"undefined-bool-conversion">;
def BoolConversion : DiagGroup<"bool-conversion", [PointerBoolConversion,
                                                   UndefinedBoolConversion]>;
def IntConversion : DiagGroup<"int-conversion">;
def EnumConversion : DiagGroup<"enum-conversion">;
def ImplicitIntConversion : DiagGroup<"implicit-int-conversion">;
def ImplicitFloatConversion : DiagGroup<"implicit-float-conversion">;

def FloatOverflowConversion : DiagGroup<"float-overflow-conversion">;
def FloatZeroConversion : DiagGroup<"float-zero-conversion">;
def FloatConversion :
  DiagGroup<"float-conversion", [FloatOverflowConversion,
                                 FloatZeroConversion]>;

def DoublePromotion : DiagGroup<"double-promotion">;
def EnumTooLarge : DiagGroup<"enum-too-large">;
def UnsupportedNan : DiagGroup<"unsupported-nan">;
def UnsupportedAbs : DiagGroup<"unsupported-abs">;
def UnsupportedCB : DiagGroup<"unsupported-cb">;
def UnsupportedGPOpt : DiagGroup<"unsupported-gpopt">;
def UnsupportedTargetOpt : DiagGroup<"unsupported-target-opt">;
def NonLiteralNullConversion : DiagGroup<"non-literal-null-conversion">;
def NullConversion : DiagGroup<"null-conversion">;
def ImplicitConversionFloatingPointToBool :
  DiagGroup<"implicit-conversion-floating-point-to-bool">;
def ObjCLiteralConversion : DiagGroup<"objc-literal-conversion">;
def MacroRedefined : DiagGroup<"macro-redefined">;
def BuiltinMacroRedefined : DiagGroup<"builtin-macro-redefined">;
def BuiltinRequiresHeader : DiagGroup<"builtin-requires-header">;
def C99Compat : DiagGroup<"c99-compat">;
def CXXCompat: DiagGroup<"c++-compat">;
def ExternCCompat : DiagGroup<"extern-c-compat">;
def KeywordCompat : DiagGroup<"keyword-compat">;
def GNUCaseRange : DiagGroup<"gnu-case-range">;
def CastAlign : DiagGroup<"cast-align">;
def CastQual : DiagGroup<"cast-qual">;
def : DiagGroup<"char-align">;
def Comment : DiagGroup<"comment">;
def GNUComplexInteger : DiagGroup<"gnu-complex-integer">;
def GNUConditionalOmittedOperand : DiagGroup<"gnu-conditional-omitted-operand">;
def ConfigMacros : DiagGroup<"config-macros">;
def : DiagGroup<"ctor-dtor-privacy">;
def GNUDesignator : DiagGroup<"gnu-designator">;
def GNUStringLiteralOperatorTemplate :
  DiagGroup<"gnu-string-literal-operator-template">;
def UndefinedVarTemplate : DiagGroup<"undefined-var-template">;
def UndefinedFuncTemplate : DiagGroup<"undefined-func-template">;
def MissingNoEscape : DiagGroup<"missing-noescape">;

def DeleteIncomplete : DiagGroup<"delete-incomplete">;
def DeleteNonAbstractNonVirtualDtor : DiagGroup<"delete-non-abstract-non-virtual-dtor">;
def DeleteAbstractNonVirtualDtor : DiagGroup<"delete-abstract-non-virtual-dtor">;
def DeleteNonVirtualDtor : DiagGroup<"delete-non-virtual-dtor",
                                     [DeleteNonAbstractNonVirtualDtor,
                                      DeleteAbstractNonVirtualDtor]>;
def AbstractFinalClass : DiagGroup<"abstract-final-class">;

def CXX11CompatDeprecatedWritableStr :
  DiagGroup<"c++11-compat-deprecated-writable-strings">;

def DeprecatedAttributes : DiagGroup<"deprecated-attributes">;
def DeprecatedDeclarations : DiagGroup<"deprecated-declarations">;
def UnavailableDeclarations : DiagGroup<"unavailable-declarations">;
def UnguardedAvailabilityNew : DiagGroup<"unguarded-availability-new">;
def UnguardedAvailability : DiagGroup<"unguarded-availability",
                                      [UnguardedAvailabilityNew]>;
// partial-availability is an alias of unguarded-availability.
def : DiagGroup<"partial-availability", [UnguardedAvailability]>;
def DeprecatedDynamicExceptionSpec
    : DiagGroup<"deprecated-dynamic-exception-spec">;
def DeprecatedImplementations :DiagGroup<"deprecated-implementations">;
def DeprecatedIncrementBool : DiagGroup<"deprecated-increment-bool">;
def DeprecatedRegister : DiagGroup<"deprecated-register">;
def DeprecatedThisCapture : DiagGroup<"deprecated-this-capture">;
def DeprecatedWritableStr : DiagGroup<"deprecated-writable-strings",
                                      [CXX11CompatDeprecatedWritableStr]>;
// FIXME: Why is DeprecatedImplementations not in this group?
def Deprecated : DiagGroup<"deprecated", [DeprecatedAttributes,
                                          DeprecatedDeclarations,
                                          DeprecatedDynamicExceptionSpec,
                                          DeprecatedIncrementBool,
                                          DeprecatedRegister,
                                          DeprecatedThisCapture,
                                          DeprecatedWritableStr]>,
                 DiagCategory<"Deprecations">;

def DynamicExceptionSpec
    : DiagGroup<"dynamic-exception-spec", [DeprecatedDynamicExceptionSpec]>;

def LibLTO : DiagGroup<"liblto">;
def : DiagGroup<"disabled-optimization">;
def : DiagGroup<"discard-qual">;
def DivZero : DiagGroup<"division-by-zero">;
def : DiagGroup<"div-by-zero", [DivZero]>;

def DocumentationHTML : DiagGroup<"documentation-html">;
def DocumentationUnknownCommand : DiagGroup<"documentation-unknown-command">;
def DocumentationPedantic : DiagGroup<"documentation-pedantic",
                                      [DocumentationUnknownCommand]>;
def DocumentationDeprecatedSync : DiagGroup<"documentation-deprecated-sync">;
def Documentation : DiagGroup<"documentation",
                              [DocumentationHTML,
                               DocumentationDeprecatedSync]>;

def EmptyBody : DiagGroup<"empty-body">;
def Exceptions : DiagGroup<"exceptions">;

def GNUEmptyInitializer : DiagGroup<"gnu-empty-initializer">;
def GNUEmptyStruct : DiagGroup<"gnu-empty-struct">;
def ExtraTokens : DiagGroup<"extra-tokens">;
def CXX98CompatExtraSemi : DiagGroup<"c++98-compat-extra-semi">;
def CXX11ExtraSemi : DiagGroup<"c++11-extra-semi">;
def EmptyInitStatement : DiagGroup<"empty-init-stmt">;
def ExtraSemiStmt : DiagGroup<"extra-semi-stmt", [EmptyInitStatement]>;
def ExtraSemi : DiagGroup<"extra-semi", [CXX98CompatExtraSemi,
                                         CXX11ExtraSemi]>;

def GNUFlexibleArrayInitializer : DiagGroup<"gnu-flexible-array-initializer">;
def GNUFlexibleArrayUnionMember : DiagGroup<"gnu-flexible-array-union-member">;
def GNUFoldingConstant : DiagGroup<"gnu-folding-constant">;
def FormatExtraArgs : DiagGroup<"format-extra-args">;
def FormatZeroLength : DiagGroup<"format-zero-length">;

def InvalidIOSDeploymentTarget : DiagGroup<"invalid-ios-deployment-target">;

def CXX17CompatMangling : DiagGroup<"c++17-compat-mangling">;
def : DiagGroup<"c++1z-compat-mangling", [CXX17CompatMangling]>;
// Name of this warning in GCC.
def NoexceptType : DiagGroup<"noexcept-type", [CXX17CompatMangling]>;

// Warnings for C++1y code which is not compatible with prior C++ standards.
def CXXPre14Compat : DiagGroup<"c++98-c++11-compat">;
def CXXPre14CompatPedantic : DiagGroup<"c++98-c++11-compat-pedantic",
                                       [CXXPre14Compat,
                                        CXXPre14CompatBinaryLiteral]>;
def CXXPre17Compat : DiagGroup<"c++98-c++11-c++14-compat">;
def CXXPre17CompatPedantic : DiagGroup<"c++98-c++11-c++14-compat-pedantic",
                                       [CXXPre17Compat]>;
def CXXPre2aCompat : DiagGroup<"c++98-c++11-c++14-c++17-compat">;
def CXXPre2aCompatPedantic : DiagGroup<"c++98-c++11-c++14-c++17-compat-pedantic",
                                       [CXXPre2aCompat]>;

def CXX98CompatBindToTemporaryCopy :
  DiagGroup<"c++98-compat-bind-to-temporary-copy">;
def CXX98CompatLocalTypeTemplateArgs :
  DiagGroup<"c++98-compat-local-type-template-args">;
def CXX98CompatUnnamedTypeTemplateArgs :
  DiagGroup<"c++98-compat-unnamed-type-template-args">;

def CXX98Compat : DiagGroup<"c++98-compat",
                            [CXX98CompatLocalTypeTemplateArgs,
                             CXX98CompatUnnamedTypeTemplateArgs,
                             CXXPre14Compat,
                             CXXPre17Compat,
                             CXXPre2aCompat]>;
// Warnings for C++11 features which are Extensions in C++98 mode.
def CXX98CompatPedantic : DiagGroup<"c++98-compat-pedantic",
                                    [CXX98Compat,
                                     CXX98CompatBindToTemporaryCopy,
                                     CXX98CompatExtraSemi,
                                     CXXPre14CompatPedantic,
                                     CXXPre17CompatPedantic,
                                     CXXPre2aCompatPedantic]>;

def CXX11Narrowing : DiagGroup<"c++11-narrowing">;

def CXX11WarnOverrideDestructor :
  DiagGroup<"inconsistent-missing-destructor-override">;
def CXX11WarnOverrideMethod : DiagGroup<"inconsistent-missing-override">;

// Original name of this warning in Clang
def : DiagGroup<"c++0x-narrowing", [CXX11Narrowing]>;

// Name of this warning in GCC
def : DiagGroup<"narrowing", [CXX11Narrowing]>;

def CXX11CompatReservedUserDefinedLiteral :
  DiagGroup<"c++11-compat-reserved-user-defined-literal">;
def ReservedUserDefinedLiteral :
  DiagGroup<"reserved-user-defined-literal",
            [CXX11CompatReservedUserDefinedLiteral]>;

def CXX11Compat : DiagGroup<"c++11-compat",
                            [CXX11Narrowing,
                             CXX11CompatReservedUserDefinedLiteral,
                             CXX11CompatDeprecatedWritableStr,
                             CXXPre14Compat,
                             CXXPre17Compat,
                             CXXPre2aCompat]>;
def : DiagGroup<"c++0x-compat", [CXX11Compat]>;
def CXX11CompatPedantic : DiagGroup<"c++11-compat-pedantic",
                                    [CXX11Compat,
                                     CXXPre14CompatPedantic,
                                     CXXPre17CompatPedantic,
                                     CXXPre2aCompatPedantic]>;

def CXX14Compat : DiagGroup<"c++14-compat", [CXXPre17Compat,
                                             CXXPre2aCompat]>;
def CXX14CompatPedantic : DiagGroup<"c++14-compat-pedantic",
                                    [CXX14Compat,
                                     CXXPre17CompatPedantic,
                                     CXXPre2aCompatPedantic]>;

def CXX17Compat : DiagGroup<"c++17-compat", [DeprecatedRegister,
                                             DeprecatedIncrementBool,
                                             CXX17CompatMangling,
                                             CXXPre2aCompat]>;
def CXX17CompatPedantic : DiagGroup<"c++17-compat-pedantic",
                                    [CXX17Compat,
                                     CXXPre2aCompatPedantic]>;
def : DiagGroup<"c++1z-compat", [CXX17Compat]>;

def CXX2aCompat : DiagGroup<"c++2a-compat">;
def CXX2aCompatPedantic : DiagGroup<"c++2a-compat-pedantic",
                                    [CXX2aCompat]>;

def ExitTimeDestructors : DiagGroup<"exit-time-destructors">;
def FlexibleArrayExtensions : DiagGroup<"flexible-array-extensions">;
def FourByteMultiChar : DiagGroup<"four-char-constants">;
def GlobalConstructors : DiagGroup<"global-constructors">;
def BitwiseOpParentheses: DiagGroup<"bitwise-op-parentheses">;
def LogicalOpParentheses: DiagGroup<"logical-op-parentheses">;
def LogicalNotParentheses: DiagGroup<"logical-not-parentheses">;
def ShiftOpParentheses: DiagGroup<"shift-op-parentheses">;
def OverloadedShiftOpParentheses: DiagGroup<"overloaded-shift-op-parentheses">;
def DanglingElse: DiagGroup<"dangling-else">;
def DanglingField : DiagGroup<"dangling-field">;
def DanglingInitializerList : DiagGroup<"dangling-initializer-list">;
def ReturnStackAddress : DiagGroup<"return-stack-address">;
def Dangling : DiagGroup<"dangling", [DanglingField,
                                      DanglingInitializerList,
                                      ReturnStackAddress]>;
def DistributedObjectModifiers : DiagGroup<"distributed-object-modifiers">;
def ExpansionToDefined : DiagGroup<"expansion-to-defined">;
def FlagEnum : DiagGroup<"flag-enum">;
def IncrementBool : DiagGroup<"increment-bool", [DeprecatedIncrementBool]>;
def InfiniteRecursion : DiagGroup<"infinite-recursion">;
def PureVirtualCallFromCtorDtor: DiagGroup<"call-to-pure-virtual-from-ctor-dtor">;
def GNUImaginaryConstant : DiagGroup<"gnu-imaginary-constant">;
def IgnoredQualifiers : DiagGroup<"ignored-qualifiers">;
def : DiagGroup<"import">;
def GNUIncludeNext : DiagGroup<"gnu-include-next">;
def IncompatibleMSStruct : DiagGroup<"incompatible-ms-struct">;
def IncompatiblePointerTypesDiscardsQualifiers
  : DiagGroup<"incompatible-pointer-types-discards-qualifiers">;
def IncompatibleFunctionPointerTypes
  : DiagGroup<"incompatible-function-pointer-types">;
def IncompatiblePointerTypes
  : DiagGroup<"incompatible-pointer-types",
    [IncompatiblePointerTypesDiscardsQualifiers,
     IncompatibleFunctionPointerTypes]>;
def IncompleteUmbrella : DiagGroup<"incomplete-umbrella">;
def IncompleteFrameworkModuleDeclaration
  : DiagGroup<"incomplete-framework-module-declaration">;
def NonModularIncludeInFrameworkModule
  : DiagGroup<"non-modular-include-in-framework-module">;
def NonModularIncludeInModule : DiagGroup<"non-modular-include-in-module",
                                          [NonModularIncludeInFrameworkModule]>;
def IncompleteModule : DiagGroup<"incomplete-module",
    [IncompleteUmbrella, NonModularIncludeInModule]>;
def PrivateModule : DiagGroup<"private-module">;

def CXX11InlineNamespace : DiagGroup<"c++11-inline-namespace">;
def InvalidNoreturn : DiagGroup<"invalid-noreturn">;
def InvalidSourceEncoding : DiagGroup<"invalid-source-encoding">;
def KNRPromotedParameter : DiagGroup<"knr-promoted-parameter">;
def : DiagGroup<"init-self">;
def : DiagGroup<"inline">;
def : DiagGroup<"invalid-pch">;
def GNULabelsAsValue : DiagGroup<"gnu-label-as-value">;
def LiteralRange : DiagGroup<"literal-range">;
def LocalTypeTemplateArgs : DiagGroup<"local-type-template-args",
                                      [CXX98CompatLocalTypeTemplateArgs]>;
def RangeLoopAnalysis : DiagGroup<"range-loop-analysis">;
def ForLoopAnalysis : DiagGroup<"for-loop-analysis">;
def LoopAnalysis : DiagGroup<"loop-analysis", [ForLoopAnalysis,
                                               RangeLoopAnalysis]>;
def MalformedWarningCheck : DiagGroup<"malformed-warning-check">;
def Main : DiagGroup<"main">;
def MainReturnType : DiagGroup<"main-return-type">;
def MaxUnsignedZero : DiagGroup<"max-unsigned-zero">;
def MissingBraces : DiagGroup<"missing-braces">;
def MissingDeclarations: DiagGroup<"missing-declarations">;
def : DiagGroup<"missing-format-attribute">;
def : DiagGroup<"missing-include-dirs">;
def MissingNoreturn : DiagGroup<"missing-noreturn">;
def MultiChar : DiagGroup<"multichar">;
def : DiagGroup<"nested-externs">;
def CXX11LongLong : DiagGroup<"c++11-long-long">;
def LongLong : DiagGroup<"long-long", [CXX11LongLong]>;
def ImplicitlyUnsignedLiteral : DiagGroup<"implicitly-unsigned-literal">;
def MethodSignatures : DiagGroup<"method-signatures">;
def MismatchedParameterTypes : DiagGroup<"mismatched-parameter-types">;
def MismatchedReturnTypes : DiagGroup<"mismatched-return-types">;
def MismatchedTags : DiagGroup<"mismatched-tags">;
def MissingFieldInitializers : DiagGroup<"missing-field-initializers">;
def ModuleBuild : DiagGroup<"module-build">;
def ModuleConflict : DiagGroup<"module-conflict">;
def ModuleFileExtension : DiagGroup<"module-file-extension">;
def NewlineEOF : DiagGroup<"newline-eof">;
def Nullability : DiagGroup<"nullability">;
def NullabilityDeclSpec : DiagGroup<"nullability-declspec">;
def NullabilityInferredOnNestedType : DiagGroup<"nullability-inferred-on-nested-type">;
def NullableToNonNullConversion : DiagGroup<"nullable-to-nonnull-conversion">;
def NullabilityCompletenessOnArrays : DiagGroup<"nullability-completeness-on-arrays">;
def NullabilityCompleteness : DiagGroup<"nullability-completeness",
                                        [NullabilityCompletenessOnArrays]>;
def NullArithmetic : DiagGroup<"null-arithmetic">;
def NullCharacter : DiagGroup<"null-character">;
def NullDereference : DiagGroup<"null-dereference">;
def InitializerOverrides : DiagGroup<"initializer-overrides">;
// For compatibility with GCC; -Woverride-init = -Winitializer-overrides
def : DiagGroup<"override-init", [InitializerOverrides]>;
def NonNull : DiagGroup<"nonnull">;
def NonPODVarargs : DiagGroup<"non-pod-varargs">;
def ClassVarargs : DiagGroup<"class-varargs", [NonPODVarargs]>;
def : DiagGroup<"nonportable-cfstrings">;
def NonVirtualDtor : DiagGroup<"non-virtual-dtor">;
def NullPointerArithmetic : DiagGroup<"null-pointer-arithmetic">;
def : DiagGroup<"effc++", [NonVirtualDtor]>;
def OveralignedType : DiagGroup<"over-aligned">;
def OldStyleCast : DiagGroup<"old-style-cast">;
def : DiagGroup<"old-style-definition">;
def OutOfLineDeclaration : DiagGroup<"out-of-line-declaration">;
def : DiagGroup<"overflow">;
def ForwardClassReceiver : DiagGroup<"receiver-forward-class">;
def MethodAccess : DiagGroup<"objc-method-access">;
def ObjCReceiver : DiagGroup<"receiver-expr">;
def OperatorNewReturnsNull : DiagGroup<"new-returns-null">;
def OverlengthStrings : DiagGroup<"overlength-strings">;
def OverloadedVirtual : DiagGroup<"overloaded-virtual">;
def PrivateExtern : DiagGroup<"private-extern">;
def SelTypeCast : DiagGroup<"cast-of-sel-type">;
def FunctionDefInObjCContainer : DiagGroup<"function-def-in-objc-container">;
def BadFunctionCast : DiagGroup<"bad-function-cast">;
def ObjCPropertyImpl : DiagGroup<"objc-property-implementation">;
def ObjCPropertyNoAttribute : DiagGroup<"objc-property-no-attribute">;
def ObjCPropertyAssignOnObjectType : DiagGroup<"objc-property-assign-on-object-type">;
def ObjCProtocolQualifiers : DiagGroup<"objc-protocol-qualifiers">;
def ObjCMissingSuperCalls : DiagGroup<"objc-missing-super-calls">;
def ObjCDesignatedInit : DiagGroup<"objc-designated-initializers">;
def ObjCRetainBlockProperty : DiagGroup<"objc-noncopy-retain-block-property">;
def ObjCReadonlyPropertyHasSetter : DiagGroup<"objc-readonly-with-setter-property">;
def ObjCInvalidIBOutletProperty : DiagGroup<"invalid-iboutlet">;
def ObjCRootClass : DiagGroup<"objc-root-class">;
def ObjCPointerIntrospectPerformSelector : DiagGroup<"deprecated-objc-pointer-introspection-performSelector">;
def ObjCPointerIntrospect : DiagGroup<"deprecated-objc-pointer-introspection", [ObjCPointerIntrospectPerformSelector]>;
def ObjCMultipleMethodNames : DiagGroup<"objc-multiple-method-names">;
def ObjCFlexibleArray : DiagGroup<"objc-flexible-array">;
def OpenCLUnsupportedRGBA: DiagGroup<"opencl-unsupported-rgba">;
def DeprecatedObjCIsaUsage : DiagGroup<"deprecated-objc-isa-usage">;
def ExplicitInitializeCall : DiagGroup<"explicit-initialize-call">;
def Packed : DiagGroup<"packed">;
def Padded : DiagGroup<"padded">;

def PessimizingMove : DiagGroup<"pessimizing-move">;
def ReturnStdMoveInCXX11 : DiagGroup<"return-std-move-in-c++11">;
def ReturnStdMove : DiagGroup<"return-std-move">;

def PointerArith : DiagGroup<"pointer-arith">;
def PoundWarning : DiagGroup<"#warnings">;
def PoundPragmaMessage : DiagGroup<"#pragma-messages">,
                         DiagCategory<"#pragma message Directive">;
def : DiagGroup<"pointer-to-int-cast">;
def : DiagGroup<"redundant-decls">;
def RedeclaredClassMember : DiagGroup<"redeclared-class-member">;
def GNURedeclaredEnum : DiagGroup<"gnu-redeclared-enum">;
def RedundantMove : DiagGroup<"redundant-move">;
def Register : DiagGroup<"register", [DeprecatedRegister]>;
def ReturnTypeCLinkage : DiagGroup<"return-type-c-linkage">;
def ReturnType : DiagGroup<"return-type", [ReturnTypeCLinkage]>;
def BindToTemporaryCopy : DiagGroup<"bind-to-temporary-copy",
                                    [CXX98CompatBindToTemporaryCopy]>;
def SelfAssignmentField : DiagGroup<"self-assign-field">;
def SelfAssignmentOverloaded : DiagGroup<"self-assign-overloaded">;
def SelfAssignment : DiagGroup<"self-assign", [SelfAssignmentOverloaded, SelfAssignmentField]>;
def SelfMove : DiagGroup<"self-move">;
def SemiBeforeMethodBody : DiagGroup<"semicolon-before-method-body">;
def Sentinel : DiagGroup<"sentinel">;
def MissingMethodReturnType : DiagGroup<"missing-method-return-type">;

def ShadowField : DiagGroup<"shadow-field">;
def ShadowFieldInConstructorModified : DiagGroup<"shadow-field-in-constructor-modified">;
def ShadowFieldInConstructor : DiagGroup<"shadow-field-in-constructor",
                                         [ShadowFieldInConstructorModified]>;
def ShadowIvar : DiagGroup<"shadow-ivar">;
def ShadowUncapturedLocal : DiagGroup<"shadow-uncaptured-local">;

// -Wshadow-all is a catch-all for all shadowing. -Wshadow is just the
// shadowing that we think is unsafe.
def Shadow : DiagGroup<"shadow", [ShadowFieldInConstructorModified,
                                  ShadowIvar]>;
def ShadowAll : DiagGroup<"shadow-all", [Shadow, ShadowFieldInConstructor,
                                         ShadowUncapturedLocal, ShadowField]>;

def Shorten64To32 : DiagGroup<"shorten-64-to-32">;
def : DiagGroup<"sign-promo">;
def SignCompare : DiagGroup<"sign-compare">;
def : DiagGroup<"stack-protector">;
def : DiagGroup<"switch-default">;
def : DiagGroup<"synth">;
def SizeofArrayArgument : DiagGroup<"sizeof-array-argument">;
def SizeofArrayDecay : DiagGroup<"sizeof-array-decay">;
def SizeofPointerMemaccess : DiagGroup<"sizeof-pointer-memaccess">;
def MemsetTransposedArgs : DiagGroup<"memset-transposed-args">;
def DynamicClassMemaccess : DiagGroup<"dynamic-class-memaccess">;
def NonTrivialMemaccess : DiagGroup<"nontrivial-memaccess">;
def SuspiciousBzero : DiagGroup<"suspicious-bzero">;
def SuspiciousMemaccess : DiagGroup<"suspicious-memaccess",
  [SizeofPointerMemaccess, DynamicClassMemaccess,
   NonTrivialMemaccess, MemsetTransposedArgs, SuspiciousBzero]>;
def StaticInInline : DiagGroup<"static-in-inline">;
def StaticLocalInInline : DiagGroup<"static-local-in-inline">;
def GNUStaticFloatInit : DiagGroup<"gnu-static-float-init">;
def StaticFloatInit : DiagGroup<"static-float-init", [GNUStaticFloatInit]>;
def GNUStatementExpression : DiagGroup<"gnu-statement-expression">;
def StringCompare : DiagGroup<"string-compare">;
def StringPlusInt : DiagGroup<"string-plus-int">;
def StringPlusChar : DiagGroup<"string-plus-char">;
def StrncatSize : DiagGroup<"strncat-size">;
def TautologicalTypeLimitCompare : DiagGroup<"tautological-type-limit-compare">;
def TautologicalUnsignedZeroCompare : DiagGroup<"tautological-unsigned-zero-compare">;
def TautologicalUnsignedEnumZeroCompare : DiagGroup<"tautological-unsigned-enum-zero-compare">;
def TautologicalInRangeCompare : DiagGroup<"tautological-constant-in-range-compare",
                                           [TautologicalTypeLimitCompare,
                                            TautologicalUnsignedZeroCompare,
                                            TautologicalUnsignedEnumZeroCompare]>;
def TautologicalOutOfRangeCompare : DiagGroup<"tautological-constant-out-of-range-compare">;
def TautologicalConstantCompare : DiagGroup<"tautological-constant-compare",
                                            [TautologicalOutOfRangeCompare]>;
def TautologicalPointerCompare : DiagGroup<"tautological-pointer-compare">;
def TautologicalOverlapCompare : DiagGroup<"tautological-overlap-compare">;
def TautologicalUndefinedCompare : DiagGroup<"tautological-undefined-compare">;
def TautologicalCompare : DiagGroup<"tautological-compare",
                                    [TautologicalConstantCompare,
                                     TautologicalPointerCompare,
                                     TautologicalOverlapCompare,
                                     TautologicalUndefinedCompare]>;
def HeaderHygiene : DiagGroup<"header-hygiene">;
def DuplicateDeclSpecifier : DiagGroup<"duplicate-decl-specifier">;
def CompareDistinctPointerType : DiagGroup<"compare-distinct-pointer-types">;
def GNUUnionCast : DiagGroup<"gnu-union-cast">;
def GNUVariableSizedTypeNotAtEnd : DiagGroup<"gnu-variable-sized-type-not-at-end">;
def Varargs : DiagGroup<"varargs">;

def Unsequenced : DiagGroup<"unsequenced">;
// GCC name for -Wunsequenced
def : DiagGroup<"sequence-point", [Unsequenced]>;

// Preprocessor warnings.
def AmbiguousMacro : DiagGroup<"ambiguous-macro">;
def KeywordAsMacro : DiagGroup<"keyword-macro">;
def ReservedIdAsMacro : DiagGroup<"reserved-id-macro">;

// Just silence warnings about -Wstrict-aliasing for now.
def : DiagGroup<"strict-aliasing=0">;
def : DiagGroup<"strict-aliasing=1">;
def : DiagGroup<"strict-aliasing=2">;
def : DiagGroup<"strict-aliasing">;

// Just silence warnings about -Wstrict-overflow for now.
def : DiagGroup<"strict-overflow=0">;
def : DiagGroup<"strict-overflow=1">;
def : DiagGroup<"strict-overflow=2">;
def : DiagGroup<"strict-overflow=3">;
def : DiagGroup<"strict-overflow=4">;
def : DiagGroup<"strict-overflow=5">;
def : DiagGroup<"strict-overflow">;

def InvalidOffsetof : DiagGroup<"invalid-offsetof">;
def : DiagGroup<"strict-prototypes">;
def StrictSelector : DiagGroup<"strict-selector-match">;
def MethodDuplicate : DiagGroup<"duplicate-method-match">;
def ObjCCStringFormat : DiagGroup<"cstring-format-directive">;
def CoveredSwitchDefault : DiagGroup<"covered-switch-default">;
def SwitchBool     : DiagGroup<"switch-bool">;
def SwitchEnum     : DiagGroup<"switch-enum">;
def Switch         : DiagGroup<"switch">;
def EnumCompareSwitch : DiagGroup<"enum-compare-switch">;
def EnumCompare       : DiagGroup<"enum-compare", [EnumCompareSwitch]>;
def ImplicitFallthroughPerFunction :
  DiagGroup<"implicit-fallthrough-per-function">;
def ImplicitFallthrough  : DiagGroup<"implicit-fallthrough",
                                     [ImplicitFallthroughPerFunction]>;
def InvalidPPToken : DiagGroup<"invalid-pp-token">;
def Trigraphs      : DiagGroup<"trigraphs">;

def : DiagGroup<"type-limits">;
def UndefinedReinterpretCast : DiagGroup<"undefined-reinterpret-cast">;
def ReinterpretBaseClass : DiagGroup<"reinterpret-base-class">;
def Unicode  : DiagGroup<"unicode">;
def UninitializedMaybe : DiagGroup<"conditional-uninitialized">;
def UninitializedSometimes : DiagGroup<"sometimes-uninitialized">;
def UninitializedStaticSelfInit : DiagGroup<"static-self-init">;
def Uninitialized  : DiagGroup<"uninitialized", [UninitializedSometimes,
                                                 UninitializedStaticSelfInit]>;
def IgnoredPragmaIntrinsic : DiagGroup<"ignored-pragma-intrinsic">;
// #pragma optimize is often used to avoid to work around MSVC codegen bugs or
// to disable inlining. It's not completely clear what alternative to suggest
// (#pragma clang optimize, noinline) so suggest nothing for now.
def IgnoredPragmaOptimize : DiagGroup<"ignored-pragma-optimize">;
def UnknownPragmas : DiagGroup<"unknown-pragmas">;
def IgnoredPragmas : DiagGroup<"ignored-pragmas",
    [IgnoredPragmaIntrinsic, IgnoredPragmaOptimize]>;
def PragmaClangAttribute : DiagGroup<"pragma-clang-attribute">;
def PragmaPackSuspiciousInclude : DiagGroup<"pragma-pack-suspicious-include">;
def PragmaPack : DiagGroup<"pragma-pack", [PragmaPackSuspiciousInclude]>;
def Pragmas : DiagGroup<"pragmas", [UnknownPragmas, IgnoredPragmas,
                                    PragmaClangAttribute, PragmaPack]>;
def UnknownWarningOption : DiagGroup<"unknown-warning-option">;
def NSobjectAttribute : DiagGroup<"NSObject-attribute">;
def NSConsumedMismatch : DiagGroup<"nsconsumed-mismatch">;
def NSReturnsMismatch : DiagGroup<"nsreturns-mismatch">;

def IndependentClassAttribute : DiagGroup<"IndependentClass-attribute">;
def UnknownAttributes : DiagGroup<"unknown-attributes">;
def IgnoredAttributes : DiagGroup<"ignored-attributes">;
def Attributes : DiagGroup<"attributes", [UnknownAttributes,
                                          IgnoredAttributes]>;
def UnknownSanitizers : DiagGroup<"unknown-sanitizers">;
def UnnamedTypeTemplateArgs : DiagGroup<"unnamed-type-template-args",
                                        [CXX98CompatUnnamedTypeTemplateArgs]>;
def UnsupportedFriend : DiagGroup<"unsupported-friend">;
def UnusedArgument : DiagGroup<"unused-argument">;
def UnusedCommandLineArgument : DiagGroup<"unused-command-line-argument">;
def IgnoredOptimizationArgument : DiagGroup<"ignored-optimization-argument">;
def InvalidCommandLineArgument : DiagGroup<"invalid-command-line-argument",
                                           [IgnoredOptimizationArgument]>;
def UnusedComparison : DiagGroup<"unused-comparison">;
def UnusedExceptionParameter : DiagGroup<"unused-exception-parameter">;
def UnneededInternalDecl : DiagGroup<"unneeded-internal-declaration">;
def UnneededMemberFunction : DiagGroup<"unneeded-member-function">;
def UnusedPrivateField : DiagGroup<"unused-private-field">;
def UnusedFunction : DiagGroup<"unused-function", [UnneededInternalDecl]>;
def UnusedTemplate : DiagGroup<"unused-template", [UnneededInternalDecl]>;
def UnusedMemberFunction : DiagGroup<"unused-member-function",
                                     [UnneededMemberFunction]>;
def UnusedLabel : DiagGroup<"unused-label">;
def UnusedLambdaCapture : DiagGroup<"unused-lambda-capture">;
def UnusedParameter : DiagGroup<"unused-parameter">;
def UnusedResult : DiagGroup<"unused-result">;
def PotentiallyEvaluatedExpression : DiagGroup<"potentially-evaluated-expression">;
def UnevaluatedExpression : DiagGroup<"unevaluated-expression",
                                      [PotentiallyEvaluatedExpression]>;
def UnusedValue : DiagGroup<"unused-value", [UnusedComparison, UnusedResult,
                                             UnevaluatedExpression]>;
def UnusedConstVariable : DiagGroup<"unused-const-variable">;
def UnusedVariable : DiagGroup<"unused-variable",
                               [UnusedConstVariable]>;
def UnusedLocalTypedef : DiagGroup<"unused-local-typedef">;
def UnusedPropertyIvar :  DiagGroup<"unused-property-ivar">;
def UnusedGetterReturnValue : DiagGroup<"unused-getter-return-value">;
def UsedButMarkedUnused : DiagGroup<"used-but-marked-unused">;
def UserDefinedLiterals : DiagGroup<"user-defined-literals">;
def UserDefinedWarnings : DiagGroup<"user-defined-warnings">;
def Reorder : DiagGroup<"reorder">;
def UndeclaredSelector : DiagGroup<"undeclared-selector">;
def ImplicitAtomic : DiagGroup<"implicit-atomic-properties">;
def CustomAtomic : DiagGroup<"custom-atomic-properties">;
def AtomicProperties : DiagGroup<"atomic-properties",
                                 [ImplicitAtomic, CustomAtomic]>;
def ARCUnsafeRetainedAssign : DiagGroup<"arc-unsafe-retained-assign">;
def ARCRetainCycles : DiagGroup<"arc-retain-cycles">;
def ARCNonPodMemAccess : DiagGroup<"arc-non-pod-memaccess">;
def AutomaticReferenceCounting : DiagGroup<"arc",
                                           [ARCUnsafeRetainedAssign,
                                            ARCRetainCycles,
                                            ARCNonPodMemAccess]>;
def ARCRepeatedUseOfWeakMaybe : DiagGroup<"arc-maybe-repeated-use-of-weak">;
def ARCRepeatedUseOfWeak : DiagGroup<"arc-repeated-use-of-weak",
                                     [ARCRepeatedUseOfWeakMaybe]>;
def BlockCaptureAutoReleasing : DiagGroup<"block-capture-autoreleasing">;
def ObjCBridge : DiagGroup<"bridge-cast">;

def DeallocInCategory:DiagGroup<"dealloc-in-category">;
def SelectorTypeMismatch : DiagGroup<"selector-type-mismatch">;
def Selector : DiagGroup<"selector", [SelectorTypeMismatch]>;
def Protocol : DiagGroup<"protocol">;
// No longer in use, preserve for backwards compatibility.
def : DiagGroup<"at-protocol">;
def PropertyAccessDotSyntax: DiagGroup<"property-access-dot-syntax">;
def PropertyAttr : DiagGroup<"property-attribute-mismatch">;
def SuperSubClassMismatch : DiagGroup<"super-class-method-mismatch">;
def OverridingMethodMismatch : DiagGroup<"overriding-method-mismatch">;
def VariadicMacros : DiagGroup<"variadic-macros">;
def VectorConversion : DiagGroup<"vector-conversion">;      // clang specific
def VexingParse : DiagGroup<"vexing-parse">;
def VLA : DiagGroup<"vla">;
def VLAExtension : DiagGroup<"vla-extension">;
def VolatileRegisterVar : DiagGroup<"volatile-register-var">;
def Visibility : DiagGroup<"visibility">;
def ZeroLengthArray : DiagGroup<"zero-length-array">;
def GNUZeroLineDirective : DiagGroup<"gnu-zero-line-directive">;
def GNUZeroVariadicMacroArguments : DiagGroup<"gnu-zero-variadic-macro-arguments">;
def Fallback : DiagGroup<"fallback">;

// This covers both the deprecated case (in C++98)
// and the extension case (in C++11 onwards).
def WritableStrings : DiagGroup<"writable-strings", [DeprecatedWritableStr]>;

// GCC calls -Wdeprecated-writable-strings -Wwrite-strings.
//
// Bizarrely, this warning flag enables -fconst-strings in C. This is
// GCC-compatible, but really weird.
//
// FIXME: Should this affect C++11 (where this is an error,
//        not just deprecated) or not?
def GCCWriteStrings : DiagGroup<"write-strings" , [WritableStrings]>;

def CharSubscript : DiagGroup<"char-subscripts">;
def LargeByValueCopy : DiagGroup<"large-by-value-copy">;
def DuplicateArgDecl : DiagGroup<"duplicate-method-arg">;
def SignedEnumBitfield : DiagGroup<"signed-enum-bitfield">;

// Unreachable code warning groups.
//
//  The goal is make -Wunreachable-code on by default, in -Wall, or at
//  least actively used, with more noisy versions of the warning covered
//  under separate flags.
//
def UnreachableCodeLoopIncrement : DiagGroup<"unreachable-code-loop-increment">;
def UnreachableCode : DiagGroup<"unreachable-code",
                                [UnreachableCodeLoopIncrement]>;
def UnreachableCodeBreak : DiagGroup<"unreachable-code-break">;
def UnreachableCodeReturn : DiagGroup<"unreachable-code-return">;
def UnreachableCodeAggressive : DiagGroup<"unreachable-code-aggressive",
                                    [UnreachableCode,
                                     UnreachableCodeBreak,
                                     UnreachableCodeReturn]>;

// Aggregation warning settings.

// Populate -Waddress with warnings from other groups.
def : DiagGroup<"address", [PointerBoolConversion,
                            StringCompare,
                            TautologicalPointerCompare]>;

// -Widiomatic-parentheses contains warnings about 'idiomatic'
// missing parentheses;  it is off by default.  We do not include it
// in -Wparentheses because most users who use -Wparentheses explicitly
// do not want these warnings.
def ParenthesesOnEquality : DiagGroup<"parentheses-equality">;
def Parentheses : DiagGroup<"parentheses",
                            [LogicalOpParentheses,
                             LogicalNotParentheses,
                             BitwiseOpParentheses,
                             ShiftOpParentheses,
                             OverloadedShiftOpParentheses,
                             ParenthesesOnEquality,
                             DanglingElse]>;

// -Wconversion has its own warnings, but we split a few out for
// legacy reasons:
//   - some people want just 64-to-32 warnings
//   - conversion warnings with constant sources are on by default
//   - conversion warnings for literals are on by default
//   - bool-to-pointer conversion warnings are on by default
//   - __null-to-integer conversion warnings are on by default
def Conversion : DiagGroup<"conversion",
                           [BoolConversion,
                            ConstantConversion,
                            EnumConversion,
                            BitFieldEnumConversion,
                            FloatConversion,
                            Shorten64To32,
                            IntConversion,
                            ImplicitIntConversion,
                            ImplicitFloatConversion,
                            LiteralConversion,
                            NonLiteralNullConversion, // (1-1)->pointer (etc)
                            NullConversion, // NULL->non-pointer
                            ObjCLiteralConversion,
                            SignConversion,
                            StringConversion]>,
                 DiagCategory<"Value Conversion Issue">;

def Unused : DiagGroup<"unused",
                       [UnusedArgument, UnusedFunction, UnusedLabel,
                        // UnusedParameter, (matches GCC's behavior)
                        // UnusedTemplate, (clean-up libc++ before enabling)
                        // UnusedMemberFunction, (clean-up llvm before enabling)
                        UnusedPrivateField, UnusedLambdaCapture,
                        UnusedLocalTypedef, UnusedValue, UnusedVariable,
                        UnusedPropertyIvar]>,
                        DiagCategory<"Unused Entity Issue">;

// Format settings.
def FormatInvalidSpecifier : DiagGroup<"format-invalid-specifier">;
def FormatSecurity : DiagGroup<"format-security">;
def FormatNonStandard : DiagGroup<"format-non-iso">;
def FormatY2K : DiagGroup<"format-y2k">;
def FormatPedantic : DiagGroup<"format-pedantic">;
def Format : DiagGroup<"format",
                       [FormatExtraArgs, FormatZeroLength, NonNull,
                        FormatSecurity, FormatY2K, FormatInvalidSpecifier]>,
             DiagCategory<"Format String Issue">;
def FormatNonLiteral : DiagGroup<"format-nonliteral">;
def Format2 : DiagGroup<"format=2",
                        [FormatNonLiteral, FormatSecurity, FormatY2K]>;

def TypeSafety : DiagGroup<"type-safety">;

def IncompatibleExceptionSpec : DiagGroup<"incompatible-exception-spec">;

def IntToVoidPointerCast : DiagGroup<"int-to-void-pointer-cast">;
def IntToPointerCast : DiagGroup<"int-to-pointer-cast",
                                 [IntToVoidPointerCast]>;

def Move : DiagGroup<"move", [
    PessimizingMove,
    RedundantMove,
    ReturnStdMove,
    SelfMove
  ]>;

def Extra : DiagGroup<"extra", [
    MissingFieldInitializers,
    IgnoredQualifiers,
    InitializerOverrides,
    SemiBeforeMethodBody,
    MissingMethodReturnType,
    SignCompare,
    UnusedParameter,
    NullPointerArithmetic,
    EmptyInitStatement
  ]>;

def Most : DiagGroup<"most", [
    CharSubscript,
    Comment,
    DeleteNonVirtualDtor,
    ForLoopAnalysis,
    Format,
    Implicit,
    InfiniteRecursion,
    MismatchedTags,
    MissingBraces,
    Move,
    MultiChar,
    Reorder,
    ReturnType,
    SelfAssignment,
    SelfMove,
    SizeofArrayArgument,
    SizeofArrayDecay,
    StringPlusInt,
    Trigraphs,
    Uninitialized,
    UnknownPragmas,
    Unused,
    VolatileRegisterVar,
    ObjCMissingSuperCalls,
    ObjCDesignatedInit,
    ObjCFlexibleArray,
    OverloadedVirtual,
    PrivateExtern,
    SelTypeCast,
    ExternCCompat,
    UserDefinedWarnings
 ]>;

// Thread Safety warnings
def ThreadSafetyAttributes : DiagGroup<"thread-safety-attributes">;
def ThreadSafetyAnalysis   : DiagGroup<"thread-safety-analysis">;
def ThreadSafetyPrecise    : DiagGroup<"thread-safety-precise">;
def ThreadSafetyReference  : DiagGroup<"thread-safety-reference">;
def ThreadSafetyNegative   : DiagGroup<"thread-safety-negative">;
def ThreadSafety : DiagGroup<"thread-safety",
                             [ThreadSafetyAttributes,
                              ThreadSafetyAnalysis,
                              ThreadSafetyPrecise,
                              ThreadSafetyReference]>;
def ThreadSafetyVerbose : DiagGroup<"thread-safety-verbose">;
def ThreadSafetyBeta : DiagGroup<"thread-safety-beta">;

// Uniqueness Analysis warnings
def Consumed       : DiagGroup<"consumed">;

// Note that putting warnings in -Wall will not disable them by default. If a
// warning should be active _only_ when -Wall is passed in, mark it as
// DefaultIgnore in addition to putting it here.
def All : DiagGroup<"all", [Most, Parentheses, Switch, SwitchBool]>;

// Warnings that should be in clang-cl /w4.
def : DiagGroup<"CL4", [All, Extra]>;

// Warnings enabled by -pedantic.  This is magically filled in by TableGen.
def Pedantic : DiagGroup<"pedantic">;

// Aliases.
def : DiagGroup<"", [Extra]>;                   // -W = -Wextra
def : DiagGroup<"endif-labels", [ExtraTokens]>; // -Wendif-labels=-Wextra-tokens
def : DiagGroup<"cpp", [PoundWarning]>;         // -Wcpp = -W#warnings
def : DiagGroup<"comments", [Comment]>;         // -Wcomments = -Wcomment
def : DiagGroup<"conversion-null",
                [NullConversion]>; // -Wconversion-null = -Wnull-conversion
def : DiagGroup<"bool-conversions",
                [BoolConversion]>; // -Wbool-conversions  = -Wbool-conversion
def : DiagGroup<"int-conversions",
                [IntConversion]>; // -Wint-conversions = -Wint-conversion
def : DiagGroup<"vector-conversions",
                [VectorConversion]>; // -Wvector-conversions = -Wvector-conversion
def : DiagGroup<"unused-local-typedefs", [UnusedLocalTypedef]>;
                // -Wunused-local-typedefs = -Wunused-local-typedef

// A warning group for warnings that we want to have on by default in clang,
// but which aren't on by default in GCC.
def NonGCC : DiagGroup<"non-gcc",
    [SignCompare, Conversion, LiteralRange]>;

// A warning group for warnings about using C++11 features as extensions in
// earlier C++ versions.
def CXX11 : DiagGroup<"c++11-extensions", [CXX11ExtraSemi, CXX11InlineNamespace,
                                           CXX11LongLong]>;

// A warning group for warnings about using C++14 features as extensions in
// earlier C++ versions.
def CXX14 : DiagGroup<"c++14-extensions", [CXX14BinaryLiteral]>;

// A warning group for warnings about using C++17 features as extensions in
// earlier C++ versions.
def CXX17 : DiagGroup<"c++17-extensions">;

// A warning group for warnings about using C++2a features as extensions in
// earlier C++ versions.
def CXX2a : DiagGroup<"c++2a-extensions">;

def : DiagGroup<"c++0x-extensions", [CXX11]>;
def : DiagGroup<"c++1y-extensions", [CXX14]>;
def : DiagGroup<"c++1z-extensions", [CXX17]>;

def DelegatingCtorCycles :
  DiagGroup<"delegating-ctor-cycles">;

// A warning group for warnings about using C11 features as extensions.
def C11 : DiagGroup<"c11-extensions">;

// A warning group for warnings about using C99 features as extensions.
def C99 : DiagGroup<"c99-extensions">;

// A warning group for warnings about GCC extensions.
def GNU : DiagGroup<"gnu", [GNUAlignofExpression, GNUAnonymousStruct,
                            GNUAutoType,
                            GNUBinaryLiteral, GNUCaseRange,
                            GNUComplexInteger, GNUCompoundLiteralInitializer,
                            GNUConditionalOmittedOperand, GNUDesignator,
                            GNUEmptyInitializer, GNUEmptyStruct,
                            VLAExtension, GNUFlexibleArrayInitializer,
                            GNUFlexibleArrayUnionMember, GNUFoldingConstant,
                            GNUImaginaryConstant, GNUIncludeNext,
                            GNULabelsAsValue,
                            RedeclaredClassMember, GNURedeclaredEnum,
                            GNUStatementExpression, GNUStaticFloatInit,
                            GNUStringLiteralOperatorTemplate,
                            GNUUnionCast, GNUVariableSizedTypeNotAtEnd,
                            ZeroLengthArray, GNUZeroLineDirective,
                            GNUZeroVariadicMacroArguments]>;
// A warning group for warnings about code that clang accepts but gcc doesn't.
def GccCompat : DiagGroup<"gcc-compat">;

// Warnings for Microsoft extensions.
def MicrosoftCharize : DiagGroup<"microsoft-charize">;
def MicrosoftInclude : DiagGroup<"microsoft-include">;
def MicrosoftCppMacro : DiagGroup<"microsoft-cpp-macro">;
def MicrosoftFixedEnum : DiagGroup<"microsoft-fixed-enum">;
def MicrosoftSealed : DiagGroup<"microsoft-sealed">;
def MicrosoftUnqualifiedFriend : DiagGroup<"microsoft-unqualified-friend">;
def MicrosoftExceptionSpec : DiagGroup<"microsoft-exception-spec">;
def MicrosoftUsingDecl : DiagGroup<"microsoft-using-decl">;
def MicrosoftMutableReference : DiagGroup<"microsoft-mutable-reference">;
def MicrosoftPureDefinition : DiagGroup<"microsoft-pure-definition">;
def MicrosoftUnionMemberReference : DiagGroup<
    "microsoft-union-member-reference">;
def MicrosoftExplicitConstructorCall : DiagGroup<
    "microsoft-explicit-constructor-call">;
def MicrosoftEnumValue : DiagGroup<"microsoft-enum-value">;
def MicrosoftDefaultArgRedefinition :
    DiagGroup<"microsoft-default-arg-redefinition">;
def MicrosoftTemplate : DiagGroup<"microsoft-template">;
def MicrosoftInconsistentDllImport : DiagGroup<"inconsistent-dllimport">;
def MicrosoftRedeclareStatic : DiagGroup<"microsoft-redeclare-static">;
def MicrosoftEnumForwardReference :
    DiagGroup<"microsoft-enum-forward-reference">;
def MicrosoftGoto : DiagGroup<"microsoft-goto">;
def MicrosoftFlexibleArray : DiagGroup<"microsoft-flexible-array">;
def MicrosoftExtraQualification : DiagGroup<"microsoft-extra-qualification">;
def MicrosoftCast : DiagGroup<"microsoft-cast">;
def MicrosoftConstInit : DiagGroup<"microsoft-const-init">;
def MicrosoftVoidPseudoDtor : DiagGroup<"microsoft-void-pseudo-dtor">;
def MicrosoftAnonTag : DiagGroup<"microsoft-anon-tag">;
def MicrosoftCommentPaste : DiagGroup<"microsoft-comment-paste">;
def MicrosoftEndOfFile : DiagGroup<"microsoft-end-of-file">;
def MicrosoftInaccessibleBase : DiagGroup<"microsoft-inaccessible-base">;
// Aliases.
def : DiagGroup<"msvc-include", [MicrosoftInclude]>;
                // -Wmsvc-include = -Wmicrosoft-include

// Warnings group for warnings about Microsoft extensions.
def Microsoft : DiagGroup<"microsoft",
    [MicrosoftCharize, MicrosoftInclude, MicrosoftCppMacro, MicrosoftFixedEnum,
     MicrosoftSealed, MicrosoftUnqualifiedFriend, MicrosoftExceptionSpec,
     MicrosoftUsingDecl, MicrosoftMutableReference, MicrosoftPureDefinition,
     MicrosoftUnionMemberReference, MicrosoftExplicitConstructorCall,
     MicrosoftEnumValue, MicrosoftDefaultArgRedefinition, MicrosoftTemplate,
     MicrosoftRedeclareStatic, MicrosoftEnumForwardReference, MicrosoftGoto,
     MicrosoftFlexibleArray, MicrosoftExtraQualification, MicrosoftCast,
     MicrosoftConstInit, MicrosoftVoidPseudoDtor, MicrosoftAnonTag,
     MicrosoftCommentPaste, MicrosoftEndOfFile,
     MicrosoftInconsistentDllImport]>;

def ClangClPch : DiagGroup<"clang-cl-pch">;

def ObjCNonUnifiedException : DiagGroup<"objc-nonunified-exceptions">;

def ObjCProtocolMethodImpl : DiagGroup<"objc-protocol-method-implementation">;

def ObjCNoPropertyAutoSynthesis : DiagGroup<"objc-property-synthesis">;

// ObjC API warning groups.
def ObjCRedundantLiteralUse : DiagGroup<"objc-redundant-literal-use">;
def ObjCRedundantAPIUse : DiagGroup<"objc-redundant-api-use", [
    ObjCRedundantLiteralUse
  ]>;

def ObjCCocoaAPI : DiagGroup<"objc-cocoa-api", [
    ObjCRedundantAPIUse
  ]>;

def ObjCStringComparison : DiagGroup<"objc-string-compare">;
def ObjCStringConcatenation : DiagGroup<"objc-string-concatenation">;
def ObjCLiteralComparison : DiagGroup<"objc-literal-compare", [
    ObjCStringComparison
  ]>;

// Inline ASM warnings.
def ASMOperandWidths : DiagGroup<"asm-operand-widths">;
def ASMIgnoredQualifier : DiagGroup<"asm-ignored-qualifier">;
def ASM : DiagGroup<"asm", [
    ASMOperandWidths, ASMIgnoredQualifier
  ]>;

// OpenMP warnings.
def SourceUsesOpenMP : DiagGroup<"source-uses-openmp">;
def OpenMPClauses : DiagGroup<"openmp-clauses">;
def OpenMPLoopForm : DiagGroup<"openmp-loop-form">;
def OpenMPTarget : DiagGroup<"openmp-target">;

// Backend warnings.
def BackendInlineAsm : DiagGroup<"inline-asm">;
def BackendFrameLargerThanEQ : DiagGroup<"frame-larger-than=">;
def BackendPlugin : DiagGroup<"backend-plugin">;
def RemarkBackendPlugin : DiagGroup<"remark-backend-plugin">;
def BackendOptimizationRemark : DiagGroup<"pass">;
def BackendOptimizationRemarkMissed : DiagGroup<"pass-missed">;
def BackendOptimizationRemarkAnalysis : DiagGroup<"pass-analysis">;
def BackendOptimizationFailure : DiagGroup<"pass-failed">;

// Instrumentation based profiling warnings.
def ProfileInstrMissing : DiagGroup<"profile-instr-missing">;
def ProfileInstrOutOfDate : DiagGroup<"profile-instr-out-of-date">;
def ProfileInstrUnprofiled : DiagGroup<"profile-instr-unprofiled">;

// AddressSanitizer frontend instrumentation remarks.
def SanitizeAddressRemarks : DiagGroup<"sanitize-address">;

// Issues with serialized diagnostics.
def SerializedDiagnostics : DiagGroup<"serialized-diagnostics">;

// A warning group for warnings about code that clang accepts when
// compiling CUDA C/C++ but which is not compatible with the CUDA spec.
def CudaCompat : DiagGroup<"cuda-compat">;

// A warning group for things that will change semantics in the future.
def FutureCompat : DiagGroup<"future-compat">;

def InvalidOrNonExistentDirectory : DiagGroup<"invalid-or-nonexistent-directory">;

def OptionIgnored : DiagGroup<"option-ignored">;

def UnknownArgument : DiagGroup<"unknown-argument">;

// A warning group for warnings about code that clang accepts when
// compiling OpenCL C/C++ but which is not compatible with the SPIR spec.
def SpirCompat : DiagGroup<"spir-compat">;

// Warning for the experimental-isel options.
def ExperimentalISel : DiagGroup<"experimental-isel">;

// A warning group specifically for warnings related to function
// multiversioning.
def FunctionMultiVersioning : DiagGroup<"function-multiversion">;

def NoDeref : DiagGroup<"noderef">;

// A group for cross translation unit static analysis related warnings.
def CrossTU : DiagGroup<"ctu">;
