//==--- Attr.td - attribute definitions -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// The documentation is organized by category. Attributes can have category-
// specific documentation that is collated within the larger document.
class DocumentationCategory<string name> {
  string Name = name;
  code Content = [{}];
}
def DocCatFunction : DocumentationCategory<"Function Attributes">;
def DocCatVariable : DocumentationCategory<"Variable Attributes">;
def DocCatType : DocumentationCategory<"Type Attributes">;
def DocCatStmt : DocumentationCategory<"Statement Attributes">;
// Attributes listed under the Undocumented category do not generate any public
// documentation. Ideally, this category should be used for internal-only
// attributes which contain no spellings.
def DocCatUndocumented : DocumentationCategory<"Undocumented">;

class DocDeprecated<string replacement = ""> {
  // If the Replacement field is empty, no replacement will be listed with the
  // documentation. Otherwise, the documentation will specify the attribute has
  // been superseded by this replacement.
  string Replacement = replacement;
}

// Specifies the documentation to be associated with the given category.
class Documentation {
  DocumentationCategory Category;
  code Content;

  // If the heading is empty, one may be picked automatically. If the attribute
  // only has one spelling, no heading is required as the attribute's sole
  // spelling is sufficient. If all spellings are semantically common, the
  // heading will be the semantic spelling. If the spellings are not
  // semantically common and no heading is provided, an error will be emitted.
  string Heading = "";

  // When set, specifies that the attribute is deprecated and can optionally
  // specify a replacement attribute.
  DocDeprecated Deprecated;
}

// Specifies that the attribute is explicitly undocumented. This can be a
// helpful placeholder for the attribute while working on the implementation,
// but should not be used once feature work has been completed.
def Undocumented : Documentation {
  let Category = DocCatUndocumented;
}

include "clang/Basic/AttrDocs.td"

// An attribute's subject is whatever it appertains to. In this file, it is
// more accurately a list of things that an attribute can appertain to. All
// Decls and Stmts are possibly AttrSubjects (even though the syntax may not
// allow attributes on a given Decl or Stmt).
class AttrSubject;

include "clang/Basic/DeclNodes.td"
include "clang/Basic/StmtNodes.td"

// A subset-subject is an AttrSubject constrained to operate only on some subset
// of that subject.
//
// The code fragment is a boolean expression that will confirm that the subject
// meets the requirements; the subject will have the name S, and will have the
// type specified by the base. It should be a simple boolean expression. The
// diagnostic string should be a comma-separated list of subject names.
class SubsetSubject<AttrSubject base, code check, string diag> : AttrSubject {
  AttrSubject Base = base;
  code CheckCode = check;
  string DiagSpelling = diag;
}

def LocalVar : SubsetSubject<Var,
                              [{S->hasLocalStorage() && !isa<ParmVarDecl>(S)}],
                              "local variables">;
def NonParmVar : SubsetSubject<Var,
                               [{S->getKind() != Decl::ParmVar}],
                               "variables">;
def NonLocalVar : SubsetSubject<Var,
                                [{!S->hasLocalStorage()}],
                                "variables with non-local storage">;
def NonBitField : SubsetSubject<Field,
                                [{!S->isBitField()}],
                                "non-bit-field non-static data members">;

def NonStaticCXXMethod : SubsetSubject<CXXMethod,
                                       [{!S->isStatic()}],
                                       "non-static member functions">;

def NonStaticNonConstCXXMethod
    : SubsetSubject<CXXMethod,
                    [{!S->isStatic() && !S->isConst()}],
                    "non-static non-const member functions">;

def ObjCInstanceMethod : SubsetSubject<ObjCMethod,
                                       [{S->isInstanceMethod()}],
                                       "Objective-C instance methods">;

def Struct : SubsetSubject<Record,
                           [{!S->isUnion()}], "structs">;

def TLSVar : SubsetSubject<Var,
                           [{S->getTLSKind() != 0}], "thread-local variables">;

def SharedVar : SubsetSubject<Var,
                              [{S->hasGlobalStorage() && !S->getTLSKind()}],
                              "global variables">;

def GlobalVar : SubsetSubject<Var,
                             [{S->hasGlobalStorage()}], "global variables">;

def InlineFunction : SubsetSubject<Function,
                             [{S->isInlineSpecified()}], "inline functions">;

// FIXME: this hack is needed because DeclNodes.td defines the base Decl node
// type to be a class, not a definition. This makes it impossible to create an
// attribute subject which accepts a Decl. Normally, this is not a problem,
// because the attribute can have no Subjects clause to accomplish this. But in
// the case of a SubsetSubject, there's no way to express it without this hack.
def DeclBase : AttrSubject;
def FunctionLike : SubsetSubject<DeclBase,
                                 [{S->getFunctionType(false) != nullptr}],
                                 "functions, function pointers">;

def OpenCLKernelFunction
    : SubsetSubject<Function, [{S->hasAttr<OpenCLKernelAttr>()}],
                    "kernel functions">;

// HasFunctionProto is a more strict version of FunctionLike, so it should
// never be specified in a Subjects list along with FunctionLike (due to the
// inclusive nature of subject testing).
def HasFunctionProto : SubsetSubject<DeclBase,
                                     [{(S->getFunctionType(true) != nullptr &&
                              isa<FunctionProtoType>(S->getFunctionType())) ||
                                       isa<ObjCMethodDecl>(S) ||
                                       isa<BlockDecl>(S)}],
                                     "non-K&R-style functions">;

// A subject that matches the implicit object parameter of a non-static member
// function. Accepted as a function type attribute on the type of such a
// member function.
// FIXME: This does not actually ever match currently.
def ImplicitObjectParameter
    : SubsetSubject<Function, [{static_cast<void>(S), false}],
                    "implicit object parameters">;

// A single argument to an attribute
class Argument<string name, bit optional, bit fake = 0> {
  string Name = name;
  bit Optional = optional;

  /// A fake argument is used to store and serialize additional information
  /// in an attribute without actually changing its parsing or pretty-printing.
  bit Fake = fake;
}

class BoolArgument<string name, bit opt = 0, bit fake = 0> : Argument<name, opt,
                                                                      fake>;
class IdentifierArgument<string name, bit opt = 0> : Argument<name, opt>;
class IntArgument<string name, bit opt = 0> : Argument<name, opt>;
class StringArgument<string name, bit opt = 0> : Argument<name, opt>;
class ExprArgument<string name, bit opt = 0> : Argument<name, opt>;
class FunctionArgument<string name, bit opt = 0, bit fake = 0> : Argument<name,
                                                                          opt,
                                                                          fake>;
class NamedArgument<string name, bit opt = 0, bit fake = 0> : Argument<name,
                                                                          opt,
                                                                          fake>;
class TypeArgument<string name, bit opt = 0> : Argument<name, opt>;
class UnsignedArgument<string name, bit opt = 0> : Argument<name, opt>;
class VariadicUnsignedArgument<string name> : Argument<name, 1>;
class VariadicExprArgument<string name> : Argument<name, 1>;
class VariadicStringArgument<string name> : Argument<name, 1>;
class VariadicIdentifierArgument<string name> : Argument<name, 1>;

// Like VariadicUnsignedArgument except values are ParamIdx.
class VariadicParamIdxArgument<string name> : Argument<name, 1>;

// Like VariadicParamIdxArgument but for a single function parameter index.
class ParamIdxArgument<string name, bit opt = 0> : Argument<name, opt>;

// A version of the form major.minor[.subminor].
class VersionArgument<string name, bit opt = 0> : Argument<name, opt>;

// This one's a doozy, so it gets its own special type
// It can be an unsigned integer, or a type. Either can
// be dependent.
class AlignedArgument<string name, bit opt = 0> : Argument<name, opt>;

// A bool argument with a default value
class DefaultBoolArgument<string name, bit default, bit fake = 0>
    : BoolArgument<name, 1, fake> {
  bit Default = default;
}

// An integer argument with a default value
class DefaultIntArgument<string name, int default> : IntArgument<name, 1> {
  int Default = default;
}

// This argument is more complex, it includes the enumerator type name,
// a list of strings to accept, and a list of enumerators to map them to.
class EnumArgument<string name, string type, list<string> values,
                   list<string> enums, bit opt = 0, bit fake = 0>
    : Argument<name, opt, fake> {
  string Type = type;
  list<string> Values = values;
  list<string> Enums = enums;
}

// FIXME: There should be a VariadicArgument type that takes any other type
//        of argument and generates the appropriate type.
class VariadicEnumArgument<string name, string type, list<string> values,
                           list<string> enums> : Argument<name, 1>  {
  string Type = type;
  list<string> Values = values;
  list<string> Enums = enums;
}

// This handles one spelling of an attribute.
class Spelling<string name, string variety> {
  string Name = name;
  string Variety = variety;
  bit KnownToGCC;
}

class GNU<string name> : Spelling<name, "GNU">;
class Declspec<string name> : Spelling<name, "Declspec">;
class Microsoft<string name> : Spelling<name, "Microsoft">;
class CXX11<string namespace, string name, int version = 1>
    : Spelling<name, "CXX11"> {
  string Namespace = namespace;
  int Version = version;
}
class C2x<string namespace, string name> : Spelling<name, "C2x"> {
  string Namespace = namespace;
}

class Keyword<string name> : Spelling<name, "Keyword">;
class Pragma<string namespace, string name> : Spelling<name, "Pragma"> {
  string Namespace = namespace;
}

// The GCC spelling implies GNU<name> and CXX11<"gnu", name> and also sets
// KnownToGCC to 1. This spelling should be used for any GCC-compatible
// attributes.
class GCC<string name> : Spelling<name, "GCC"> {
  let KnownToGCC = 1;
}

// The Clang spelling implies GNU<name>, CXX11<"clang", name>, and optionally,
// C2x<"clang", name>. This spelling should be used for any Clang-specific
// attributes.
class Clang<string name, bit allowInC = 1> : Spelling<name, "Clang"> {
  bit AllowInC = allowInC;
}

class Accessor<string name, list<Spelling> spellings> {
  string Name = name;
  list<Spelling> Spellings = spellings;
}

class SubjectDiag<bit warn> {
  bit Warn = warn;
}
def WarnDiag : SubjectDiag<1>;
def ErrorDiag : SubjectDiag<0>;

class SubjectList<list<AttrSubject> subjects, SubjectDiag diag = WarnDiag,
                  string customDiag = ""> {
  list<AttrSubject> Subjects = subjects;
  SubjectDiag Diag = diag;
  string CustomDiag = customDiag;
}

class LangOpt<string name, bit negated = 0> {
  string Name = name;
  bit Negated = negated;
}
def MicrosoftExt : LangOpt<"MicrosoftExt">;
def Borland : LangOpt<"Borland">;
def CUDA : LangOpt<"CUDA">;
def COnly : LangOpt<"CPlusPlus", 1>;
def CPlusPlus : LangOpt<"CPlusPlus">;
def OpenCL : LangOpt<"OpenCL">;
def RenderScript : LangOpt<"RenderScript">;
def ObjC : LangOpt<"ObjC">;
def BlocksSupported : LangOpt<"Blocks">;
def ObjCAutoRefCount : LangOpt<"ObjCAutoRefCount">;

// Defines targets for target-specific attributes. Empty lists are unchecked.
class TargetSpec {
  // Specifies Architectures for which the target applies, based off the
  // ArchType enumeration in Triple.h.
  list<string> Arches = [];
  // Specifies Operating Systems for which the target applies, based off the
  // OSType enumeration in Triple.h
  list<string> OSes;
  // Specifies the C++ ABIs for which the target applies, based off the
  // TargetCXXABI::Kind in TargetCXXABI.h.
  list<string> CXXABIs;
  // Specifies Object Formats for which the target applies, based off the
  // ObjectFormatType enumeration in Triple.h
  list<string> ObjectFormats;
}

class TargetArch<list<string> arches> : TargetSpec {
  let Arches = arches;
}
def TargetARM : TargetArch<["arm", "thumb", "armeb", "thumbeb"]>;
def TargetAVR : TargetArch<["avr"]>;
def TargetMips32 : TargetArch<["mips", "mipsel"]>;
def TargetAnyMips : TargetArch<["mips", "mipsel", "mips64", "mips64el"]>;
def TargetMSP430 : TargetArch<["msp430"]>;
def TargetRISCV : TargetArch<["riscv32", "riscv64"]>;
def TargetX86 : TargetArch<["x86"]>;
def TargetAnyX86 : TargetArch<["x86", "x86_64"]>;
def TargetWebAssembly : TargetArch<["wasm32", "wasm64"]>;
def TargetWindows : TargetArch<["x86", "x86_64", "arm", "thumb", "aarch64"]> {
  let OSes = ["Win32"];
}
def TargetMicrosoftCXXABI : TargetArch<["x86", "x86_64", "arm", "thumb", "aarch64"]> {
  let CXXABIs = ["Microsoft"];
}
def TargetELF : TargetSpec {
  let ObjectFormats = ["ELF"];
}

// Attribute subject match rules that are used for #pragma clang attribute.
//
// A instance of AttrSubjectMatcherRule represents an individual match rule.
// An individual match rule can correspond to a number of different attribute
// subjects, e.g. "record" matching rule corresponds to the Record and
// CXXRecord attribute subjects.
//
// Match rules are used in the subject list of the #pragma clang attribute.
// Match rules can have sub-match rules that are instances of
// AttrSubjectMatcherSubRule. A sub-match rule can correspond to a number
// of different attribute subjects, and it can have a negated spelling as well.
// For example, "variable(unless(is_parameter))" matching rule corresponds to
// the NonParmVar attribute subject.
class AttrSubjectMatcherSubRule<string name, list<AttrSubject> subjects,
                                bit negated = 0> {
  string Name = name;
  list<AttrSubject> Subjects = subjects;
  bit Negated = negated;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, the language options are taken
  // from the parent matcher rule.
  list<LangOpt> LangOpts = [];
}
class AttrSubjectMatcherRule<string name, list<AttrSubject> subjects,
                             list<AttrSubjectMatcherSubRule> subrules = []> {
  string Name = name;
  list<AttrSubject> Subjects = subjects;
  list<AttrSubjectMatcherSubRule> Constraints = subrules;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, no language options are required.
  list<LangOpt> LangOpts = [];
}

// function(is_member)
def SubRuleForCXXMethod : AttrSubjectMatcherSubRule<"is_member", [CXXMethod]> {
  let LangOpts = [CPlusPlus];
}
def SubjectMatcherForFunction : AttrSubjectMatcherRule<"function", [Function], [
  SubRuleForCXXMethod
]>;
// hasType is abstract, it should be used with one of the sub-rules.
def SubjectMatcherForType : AttrSubjectMatcherRule<"hasType", [], [
  AttrSubjectMatcherSubRule<"functionType", [FunctionLike]>

  // FIXME: There's a matcher ambiguity with objc methods and blocks since
  // functionType excludes them but functionProtoType includes them.
  // AttrSubjectMatcherSubRule<"functionProtoType", [HasFunctionProto]>
]>;
def SubjectMatcherForTypedef : AttrSubjectMatcherRule<"type_alias",
                                                      [TypedefName]>;
def SubjectMatcherForRecord : AttrSubjectMatcherRule<"record", [Record,
                                                                CXXRecord], [
  // unless(is_union)
  AttrSubjectMatcherSubRule<"is_union", [Struct], 1>
]>;
def SubjectMatcherForEnum : AttrSubjectMatcherRule<"enum", [Enum]>;
def SubjectMatcherForEnumConstant : AttrSubjectMatcherRule<"enum_constant",
                                                           [EnumConstant]>;
def SubjectMatcherForVar : AttrSubjectMatcherRule<"variable", [Var], [
  AttrSubjectMatcherSubRule<"is_thread_local", [TLSVar]>,
  AttrSubjectMatcherSubRule<"is_global", [GlobalVar]>,
  AttrSubjectMatcherSubRule<"is_parameter", [ParmVar]>,
  // unless(is_parameter)
  AttrSubjectMatcherSubRule<"is_parameter", [NonParmVar], 1>
]>;
def SubjectMatcherForField : AttrSubjectMatcherRule<"field", [Field]>;
def SubjectMatcherForNamespace : AttrSubjectMatcherRule<"namespace",
                                                        [Namespace]> {
  let LangOpts = [CPlusPlus];
}
def SubjectMatcherForObjCInterface : AttrSubjectMatcherRule<"objc_interface",
                                                            [ObjCInterface]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCProtocol : AttrSubjectMatcherRule<"objc_protocol",
                                                           [ObjCProtocol]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCCategory : AttrSubjectMatcherRule<"objc_category",
                                                           [ObjCCategory]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCMethod : AttrSubjectMatcherRule<"objc_method",
                                                         [ObjCMethod], [
  AttrSubjectMatcherSubRule<"is_instance", [ObjCInstanceMethod]>
]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCProperty : AttrSubjectMatcherRule<"objc_property",
                                                           [ObjCProperty]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForBlock : AttrSubjectMatcherRule<"block", [Block]> {
  let LangOpts = [BlocksSupported];
}

// Aggregate attribute subject match rules are abstract match rules that can't
// be used directly in #pragma clang attribute. Instead, users have to use
// subject match rules that correspond to attribute subjects that derive from
// the specified subject.
class AttrSubjectMatcherAggregateRule<AttrSubject subject> {
  AttrSubject Subject = subject;
}

def SubjectMatcherForNamed : AttrSubjectMatcherAggregateRule<Named>;

class Attr {
  // The various ways in which an attribute can be spelled in source
  list<Spelling> Spellings;
  // The things to which an attribute can appertain
  SubjectList Subjects;
  // The arguments allowed on an attribute
  list<Argument> Args = [];
  // Accessors which should be generated for the attribute.
  list<Accessor> Accessors = [];
  // Set to true for attributes with arguments which require delayed parsing.
  bit LateParsed = 0;
  // Set to false to prevent an attribute from being propagated from a template
  // to the instantiation.
  bit Clone = 1;
  // Set to true for attributes which must be instantiated within templates
  bit TemplateDependent = 0;
  // Set to true for attributes that have a corresponding AST node.
  bit ASTNode = 1;
  // Set to true for attributes which have handler in Sema.
  bit SemaHandler = 1;
  // Set to true for attributes that are completely ignored.
  bit Ignored = 0;
  // Set to true if the attribute's parsing does not match its semantic
  // content. Eg) It parses 3 args, but semantically takes 4 args.  Opts out of
  // common attribute error checking.
  bit HasCustomParsing = 0;
  // Set to true if all of the attribute's arguments should be parsed in an
  // unevaluated context.
  bit ParseArgumentsAsUnevaluated = 0;
  // Set to true if this attribute meaningful when applied to or inherited
  // in a class template definition.
  bit MeaningfulToClassTemplateDefinition = 0;
  // Set to true if this attribute can be used with '#pragma clang attribute'.
  // By default, an attribute is supported by the '#pragma clang attribute'
  // only when:
  // - It has a subject list whose subjects can be represented using subject
  //   match rules.
  // - It has GNU/CXX11 spelling and doesn't require delayed parsing.
  bit PragmaAttributeSupport;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, no language options are required.
  list<LangOpt> LangOpts = [];
  // Any additional text that should be included verbatim in the class.
  // Note: Any additional data members will leak and should be constructed
  // externally on the ASTContext.
  code AdditionalMembers = [{}];
  // Any documentation that should be associated with the attribute. Since an
  // attribute may be documented under multiple categories, more than one
  // Documentation entry may be listed.
  list<Documentation> Documentation;
}

/// A type attribute is not processed on a declaration or a statement.
class TypeAttr : Attr;

/// A stmt attribute is not processed on a declaration or a type.
class StmtAttr : Attr;

/// An inheritable attribute is inherited by later redeclarations.
class InheritableAttr : Attr {
  // Set to true if this attribute can be duplicated on a subject when inheriting
  // attributes from prior declarations.
  bit InheritEvenIfAlreadyPresent = 0;
}

/// Some attributes, like calling conventions, can appear in either the
/// declaration or the type position. These attributes are morally type
/// attributes, but have historically been written on declarations.
class DeclOrTypeAttr : InheritableAttr;

/// A target-specific attribute.  This class is meant to be used as a mixin
/// with InheritableAttr or Attr depending on the attribute's needs.
class TargetSpecificAttr<TargetSpec target> {
  TargetSpec Target = target;
  // Attributes are generally required to have unique spellings for their names
  // so that the parser can determine what kind of attribute it has parsed.
  // However, target-specific attributes are special in that the attribute only
  // "exists" for a given target. So two target-specific attributes can share
  // the same name when they exist in different targets. To support this, a
  // Kind can be explicitly specified for a target-specific attribute. This
  // corresponds to the ParsedAttr::AT_* enum that is generated and it
  // should contain a shared value between the attributes.
  //
  // Target-specific attributes which use this feature should ensure that the
  // spellings match exactly between the attributes, and if the arguments or
  // subjects differ, should specify HasCustomParsing = 1 and implement their
  // own parsing and semantic handling requirements as-needed.
  string ParseKind;
}

/// An inheritable parameter attribute is inherited by later
/// redeclarations, even when it's written on a parameter.
class InheritableParamAttr : InheritableAttr;

/// An attribute which changes the ABI rules for a specific parameter.
class ParameterABIAttr : InheritableParamAttr {
  let Subjects = SubjectList<[ParmVar]>;
}

/// An ignored attribute, which we parse but discard with no checking.
class IgnoredAttr : Attr {
  let Ignored = 1;
  let ASTNode = 0;
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

//
// Attributes begin here
//

def AbiTag : Attr {
  let Spellings = [GCC<"abi_tag">];
  let Args = [VariadicStringArgument<"Tags">];
  let Subjects = SubjectList<[Struct, Var, Function, Namespace], ErrorDiag>;
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [AbiTagsDocs];
}

def AddressSpace : TypeAttr {
  let Spellings = [Clang<"address_space">];
  let Args = [IntArgument<"AddressSpace">];
  let Documentation = [Undocumented];
}

def Alias : Attr {
  let Spellings = [GCC<"alias">];
  let Args = [StringArgument<"Aliasee">];
  let Subjects = SubjectList<[Function, GlobalVar], ErrorDiag>;
  let Documentation = [Undocumented];
}

def Aligned : InheritableAttr {
  let Spellings = [GCC<"aligned">, Declspec<"align">, Keyword<"alignas">,
                   Keyword<"_Alignas">];
  let Args = [AlignedArgument<"Alignment", 1>];
  let Accessors = [Accessor<"isGNU", [GCC<"aligned">]>,
                   Accessor<"isC11", [Keyword<"_Alignas">]>,
                   Accessor<"isAlignas", [Keyword<"alignas">,
                                          Keyword<"_Alignas">]>,
                   Accessor<"isDeclspec",[Declspec<"align">]>];
  let Documentation = [Undocumented];
}

def AlignValue : Attr {
  let Spellings = [
    // Unfortunately, this is semantically an assertion, not a directive
    // (something else must ensure the alignment), so aligned_value is a
    // probably a better name. We might want to add an aligned_value spelling in
    // the future (and a corresponding C++ attribute), but this can be done
    // later once we decide if we also want them to have slightly-different
    // semantics than Intel's align_value.
    //
    // Does not get a [[]] spelling because the attribute is not exposed as such
    // by Intel.
    GNU<"align_value">
    // Intel's compiler on Windows also supports:
    // , Declspec<"align_value">
  ];
  let Args = [ExprArgument<"Alignment">];
  let Subjects = SubjectList<[Var, TypedefName]>;
  let Documentation = [AlignValueDocs];
}

def AlignMac68k : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def AlwaysInline : InheritableAttr {
  let Spellings = [GCC<"always_inline">, Keyword<"__forceinline">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def Artificial : InheritableAttr {
  let Spellings = [GCC<"artificial">];
  let Subjects = SubjectList<[InlineFunction], WarnDiag>;
  let Documentation = [ArtificialDocs];
}

def XRayInstrument : InheritableAttr {
  let Spellings = [Clang<"xray_always_instrument">,
                   Clang<"xray_never_instrument">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Accessors = [Accessor<"alwaysXRayInstrument",
                     [Clang<"xray_always_instrument">]>,
                   Accessor<"neverXRayInstrument",
                     [Clang<"xray_never_instrument">]>];
  let Documentation = [XRayDocs];
}

def XRayLogArgs : InheritableAttr {
  let Spellings = [Clang<"xray_log_args">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  // This argument is a count not an index, so it has the same encoding (base
  // 1 including C++ implicit this parameter) at the source and LLVM levels of
  // representation, so ParamIdxArgument is inappropriate.  It is never used
  // at the AST level of representation, so it never needs to be adjusted not
  // to include any C++ implicit this parameter.  Thus, we just store it and
  // use it as an unsigned that never needs adjustment.
  let Args = [UnsignedArgument<"ArgumentCount">];
  let Documentation = [XRayDocs];
}

def TLSModel : InheritableAttr {
  let Spellings = [GCC<"tls_model">];
  let Subjects = SubjectList<[TLSVar], ErrorDiag>;
  let Args = [StringArgument<"Model">];
  let Documentation = [TLSModelDocs];
}

def AnalyzerNoReturn : InheritableAttr {
  // TODO: should this attribute be exposed with a [[]] spelling under the clang
  // vendor namespace, or should it use a vendor namespace specific to the
  // analyzer?
  let Spellings = [GNU<"analyzer_noreturn">];
  // TODO: Add subject list.
  let Documentation = [Undocumented];
}

def Annotate : InheritableParamAttr {
  let Spellings = [Clang<"annotate">];
  let Args = [StringArgument<"Annotation">];
  // Ensure that the annotate attribute can be used with
  // '#pragma clang attribute' even though it has no subject list.
  let PragmaAttributeSupport = 1;
  let Documentation = [Undocumented];
}

def ARMInterrupt : InheritableAttr, TargetSpecificAttr<TargetARM> {
  // NOTE: If you add any additional spellings, MSP430Interrupt's,
  // MipsInterrupt's and AnyX86Interrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Args = [EnumArgument<"Interrupt", "InterruptType",
                           ["IRQ", "FIQ", "SWI", "ABORT", "UNDEF", ""],
                           ["IRQ", "FIQ", "SWI", "ABORT", "UNDEF", "Generic"],
                           1>];
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [ARMInterruptDocs];
}

def AVRInterrupt : InheritableAttr, TargetSpecificAttr<TargetAVR> {
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let ParseKind = "Interrupt";
  let Documentation = [AVRInterruptDocs];
}

def AVRSignal : InheritableAttr, TargetSpecificAttr<TargetAVR> {
  let Spellings = [GCC<"signal">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [AVRSignalDocs];
}

def AsmLabel : InheritableAttr {
  let Spellings = [Keyword<"asm">, Keyword<"__asm__">];
  let Args = [StringArgument<"Label">];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def Availability : InheritableAttr {
  let Spellings = [Clang<"availability">];
  let Args = [IdentifierArgument<"platform">, VersionArgument<"introduced">,
              VersionArgument<"deprecated">, VersionArgument<"obsoleted">,
              BoolArgument<"unavailable">, StringArgument<"message">,
              BoolArgument<"strict">, StringArgument<"replacement">];
  let AdditionalMembers =
[{static llvm::StringRef getPrettyPlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("android", "Android")
             .Case("ios", "iOS")
             .Case("macos", "macOS")
             .Case("tvos", "tvOS")
             .Case("watchos", "watchOS")
             .Case("ios_app_extension", "iOS (App Extension)")
             .Case("macos_app_extension", "macOS (App Extension)")
             .Case("tvos_app_extension", "tvOS (App Extension)")
             .Case("watchos_app_extension", "watchOS (App Extension)")
             .Case("swift", "Swift")
             .Default(llvm::StringRef());
}
static llvm::StringRef getPlatformNameSourceSpelling(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("ios", "iOS")
             .Case("macos", "macOS")
             .Case("tvos", "tvOS")
             .Case("watchos", "watchOS")
             .Case("ios_app_extension", "iOSApplicationExtension")
             .Case("macos_app_extension", "macOSApplicationExtension")
             .Case("tvos_app_extension", "tvOSApplicationExtension")
             .Case("watchos_app_extension", "watchOSApplicationExtension")
             .Default(Platform);
}
static llvm::StringRef canonicalizePlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("iOS", "ios")
             .Case("macOS", "macos")
             .Case("tvOS", "tvos")
             .Case("watchOS", "watchos")
             .Case("iOSApplicationExtension", "ios_app_extension")
             .Case("macOSApplicationExtension", "macos_app_extension")
             .Case("tvOSApplicationExtension", "tvos_app_extension")
             .Case("watchOSApplicationExtension", "watchos_app_extension")
             .Default(Platform);
} }];
  let HasCustomParsing = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Named]>;
  let Documentation = [AvailabilityDocs];
}

def ExternalSourceSymbol : InheritableAttr {
  let Spellings = [Clang<"external_source_symbol">];
  let Args = [StringArgument<"language", 1>,
              StringArgument<"definedIn", 1>,
              BoolArgument<"generatedDeclaration", 1>];
  let HasCustomParsing = 1;
  let Subjects = SubjectList<[Named]>;
  let Documentation = [ExternalSourceSymbolDocs];
}

def Blocks : InheritableAttr {
  let Spellings = [Clang<"blocks">];
  let Args = [EnumArgument<"Type", "BlockType", ["byref"], ["ByRef"]>];
  let Documentation = [Undocumented];
}

def Bounded : IgnoredAttr {
  // Does not have a [[]] spelling because the attribute is ignored.
  let Spellings = [GNU<"bounded">];
}

def CarriesDependency : InheritableParamAttr {
  let Spellings = [GNU<"carries_dependency">,
                   CXX11<"","carries_dependency", 200809>];
  let Subjects = SubjectList<[ParmVar, ObjCMethod, Function], ErrorDiag>;
  let Documentation = [CarriesDependencyDocs];
}

def CDecl : DeclOrTypeAttr {
  let Spellings = [GCC<"cdecl">, Keyword<"__cdecl">, Keyword<"_cdecl">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

// cf_audited_transfer indicates that the given function has been
// audited and has been marked with the appropriate cf_consumed and
// cf_returns_retained attributes.  It is generally applied by
// '#pragma clang arc_cf_code_audited' rather than explicitly.
def CFAuditedTransfer : InheritableAttr {
  let Spellings = [Clang<"cf_audited_transfer">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

// cf_unknown_transfer is an explicit opt-out of cf_audited_transfer.
// It indicates that the function has unknown or unautomatable
// transfer semantics.
def CFUnknownTransfer : InheritableAttr {
  let Spellings = [Clang<"cf_unknown_transfer">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def CFReturnsRetained : InheritableAttr {
  let Spellings = [Clang<"cf_returns_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def CFReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"cf_returns_not_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def CFConsumed : InheritableParamAttr {
  let Spellings = [Clang<"cf_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

// OSObject-based attributes.
def OSConsumed : InheritableParamAttr {
  let Spellings = [Clang<"os_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetained : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty, ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"os_returns_not_retained">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty, ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetainedOnZero : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained_on_zero">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetainedOnNonZero : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained_on_non_zero">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSConsumesThis : InheritableAttr {
  let Spellings = [Clang<"os_consumes_this">];
  let Subjects = SubjectList<[NonStaticCXXMethod]>;
  let Documentation = [RetainBehaviorDocs];
}

def Cleanup : InheritableAttr {
  let Spellings = [GCC<"cleanup">];
  let Args = [FunctionArgument<"FunctionDecl">];
  let Subjects = SubjectList<[LocalVar]>;
  let Documentation = [Undocumented];
}

def Cold : InheritableAttr {
  let Spellings = [GCC<"cold">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def Common : InheritableAttr {
  let Spellings = [GCC<"common">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [Undocumented];
}

def Const : InheritableAttr {
  let Spellings = [GCC<"const">, GCC<"__const">];
  let Documentation = [Undocumented];
}

def Constructor : InheritableAttr {
  let Spellings = [GCC<"constructor">];
  let Args = [DefaultIntArgument<"Priority", 65535>];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def CPUSpecific : InheritableAttr {
  let Spellings = [Clang<"cpu_specific">, Declspec<"cpu_specific">];
  let Args = [VariadicIdentifierArgument<"Cpus">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CPUSpecificCPUDispatchDocs];
  let AdditionalMembers = [{
    IdentifierInfo *getCPUName(unsigned Index) const {
      return *(cpus_begin() + Index);
    }
  }];
}

def CPUDispatch : InheritableAttr {
  let Spellings = [Clang<"cpu_dispatch">, Declspec<"cpu_dispatch">];
  let Args = [VariadicIdentifierArgument<"Cpus">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CPUSpecificCPUDispatchDocs];
}

// CUDA attributes are spelled __attribute__((attr)) or __declspec(__attr__),
// and they do not receive a [[]] spelling.
def CUDAConstant : InheritableAttr {
  let Spellings = [GNU<"constant">, Declspec<"__constant__">];
  let Subjects = SubjectList<[Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDACudartBuiltin : IgnoredAttr {
  let Spellings = [GNU<"cudart_builtin">, Declspec<"__cudart_builtin__">];
  let LangOpts = [CUDA];
}

def CUDADevice : InheritableAttr {
  let Spellings = [GNU<"device">, Declspec<"__device__">];
  let Subjects = SubjectList<[Function, Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDADeviceBuiltin : IgnoredAttr {
  let Spellings = [GNU<"device_builtin">, Declspec<"__device_builtin__">];
  let LangOpts = [CUDA];
}

def CUDADeviceBuiltinSurfaceType : IgnoredAttr {
  let Spellings = [GNU<"device_builtin_surface_type">,
                   Declspec<"__device_builtin_surface_type__">];
  let LangOpts = [CUDA];
}

def CUDADeviceBuiltinTextureType : IgnoredAttr {
  let Spellings = [GNU<"device_builtin_texture_type">,
                   Declspec<"__device_builtin_texture_type__">];
  let LangOpts = [CUDA];
}

def CUDAGlobal : InheritableAttr {
  let Spellings = [GNU<"global">, Declspec<"__global__">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDAHost : InheritableAttr {
  let Spellings = [GNU<"host">, Declspec<"__host__">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDAInvalidTarget : InheritableAttr {
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDALaunchBounds : InheritableAttr {
  let Spellings = [GNU<"launch_bounds">, Declspec<"__launch_bounds__">];
  let Args = [ExprArgument<"MaxThreads">, ExprArgument<"MinBlocks", 1>];
  let LangOpts = [CUDA];
  let Subjects = SubjectList<[ObjCMethod, FunctionLike]>;
  // An AST node is created for this attribute, but is not used by other parts
  // of the compiler. However, this node needs to exist in the AST because
  // non-LLVM backends may be relying on the attribute's presence.
  let Documentation = [Undocumented];
}

def CUDAShared : InheritableAttr {
  let Spellings = [GNU<"shared">, Declspec<"__shared__">];
  let Subjects = SubjectList<[Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def C11NoReturn : InheritableAttr {
  let Spellings = [Keyword<"_Noreturn">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let SemaHandler = 0;
  let Documentation = [C11NoReturnDocs];
}

def CXX11NoReturn : InheritableAttr {
  let Spellings = [CXX11<"", "noreturn", 200809>];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [CXX11NoReturnDocs];
}

// Similar to CUDA, OpenCL attributes do not receive a [[]] spelling because
// the specification does not expose them with one currently.
def OpenCLKernel : InheritableAttr {
  let Spellings = [Keyword<"__kernel">, Keyword<"kernel">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def OpenCLUnrollHint : InheritableAttr {
  let Spellings = [GNU<"opencl_unroll_hint">];
  let Args = [UnsignedArgument<"UnrollHint">];
  let Documentation = [OpenCLUnrollHintDocs];
}

def OpenCLIntelReqdSubGroupSize: InheritableAttr {
  let Spellings = [GNU<"intel_reqd_sub_group_size">];
  let Args = [UnsignedArgument<"SubGroupSize">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [OpenCLIntelReqdSubGroupSizeDocs];
}

// This attribute is both a type attribute, and a declaration attribute (for
// parameter variables).
def OpenCLAccess : Attr {
  let Spellings = [Keyword<"__read_only">, Keyword<"read_only">,
                   Keyword<"__write_only">, Keyword<"write_only">,
                   Keyword<"__read_write">, Keyword<"read_write">];
  let Subjects = SubjectList<[ParmVar, TypedefName], ErrorDiag>;
  let Accessors = [Accessor<"isReadOnly", [Keyword<"__read_only">,
                                           Keyword<"read_only">]>,
                   Accessor<"isReadWrite", [Keyword<"__read_write">,
                                            Keyword<"read_write">]>,
                   Accessor<"isWriteOnly", [Keyword<"__write_only">,
                                            Keyword<"write_only">]>];
  let Documentation = [OpenCLAccessDocs];
}

def OpenCLPrivateAddressSpace : TypeAttr {
  let Spellings = [Keyword<"__private">, Keyword<"private">];
  let Documentation = [OpenCLAddressSpacePrivateDocs];
}

def OpenCLGlobalAddressSpace : TypeAttr {
  let Spellings = [Keyword<"__global">, Keyword<"global">];
  let Documentation = [OpenCLAddressSpaceGlobalDocs];
}

def OpenCLLocalAddressSpace : TypeAttr {
  let Spellings = [Keyword<"__local">, Keyword<"local">];
  let Documentation = [OpenCLAddressSpaceLocalDocs];
}

def OpenCLConstantAddressSpace : TypeAttr {
  let Spellings = [Keyword<"__constant">, Keyword<"constant">];
  let Documentation = [OpenCLAddressSpaceConstantDocs];
}

def OpenCLGenericAddressSpace : TypeAttr {
  let Spellings = [Keyword<"__generic">, Keyword<"generic">];
  let Documentation = [OpenCLAddressSpaceGenericDocs];
}

def OpenCLNoSVM : Attr {
  let Spellings = [GNU<"nosvm">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [OpenCLNoSVMDocs];
  let LangOpts = [OpenCL];
  let ASTNode = 0;
}

def RenderScriptKernel : Attr {
  let Spellings = [GNU<"kernel">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [RenderScriptKernelAttributeDocs];
  let LangOpts = [RenderScript];
}

def Deprecated : InheritableAttr {
  let Spellings = [GCC<"deprecated">, Declspec<"deprecated">,
                   CXX11<"","deprecated", 201309>, C2x<"", "deprecated">];
  let Args = [StringArgument<"Message", 1>,
              // An optional string argument that enables us to provide a
              // Fix-It.
              StringArgument<"Replacement", 1>];
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [DeprecatedDocs];
}

def Destructor : InheritableAttr {
  let Spellings = [GCC<"destructor">];
  let Args = [DefaultIntArgument<"Priority", 65535>];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def EmptyBases : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"empty_bases">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [EmptyBasesDocs];
}

def AllocSize : InheritableAttr {
  let Spellings = [GCC<"alloc_size">];
  let Subjects = SubjectList<[Function]>;
  let Args = [ParamIdxArgument<"ElemSizeParam">,
              ParamIdxArgument<"NumElemsParam", /*opt*/ 1>];
  let TemplateDependent = 1;
  let Documentation = [AllocSizeDocs];
}

def EnableIf : InheritableAttr {
  // Does not have a [[]] spelling because this attribute requires the ability
  // to parse function arguments but the attribute is not written in the type
  // position.
  let Spellings = [GNU<"enable_if">];
  let Subjects = SubjectList<[Function]>;
  let Args = [ExprArgument<"Cond">, StringArgument<"Message">];
  let TemplateDependent = 1;
  let Documentation = [EnableIfDocs];
}

def ExtVectorType : Attr {
  // This is an OpenCL-related attribute and does not receive a [[]] spelling.
  let Spellings = [GNU<"ext_vector_type">];
  // FIXME: This subject list is wrong; this is a type attribute.
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Args = [ExprArgument<"NumElements">];
  let ASTNode = 0;
  let Documentation = [Undocumented];
  // This is a type attribute with an incorrect subject list, so should not be
  // permitted by #pragma clang attribute.
  let PragmaAttributeSupport = 0;
}

def FallThrough : StmtAttr {
  let Spellings = [CXX11<"", "fallthrough", 201603>, C2x<"", "fallthrough">,
                   CXX11<"clang", "fallthrough">];
//  let Subjects = [NullStmt];
  let Documentation = [FallthroughDocs];
}

def FastCall : DeclOrTypeAttr {
  let Spellings = [GCC<"fastcall">, Keyword<"__fastcall">,
                   Keyword<"_fastcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [FastCallDocs];
}

def RegCall : DeclOrTypeAttr {
  let Spellings = [GCC<"regcall">, Keyword<"__regcall">];
  let Documentation = [RegCallDocs];
}

def Final : InheritableAttr {
  let Spellings = [Keyword<"final">, Keyword<"sealed">];
  let Accessors = [Accessor<"isSpelledAsSealed", [Keyword<"sealed">]>];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def MinSize : InheritableAttr {
  let Spellings = [Clang<"minsize">];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [Undocumented];
}

def FlagEnum : InheritableAttr {
  let Spellings = [Clang<"flag_enum">];
  let Subjects = SubjectList<[Enum]>;
  let Documentation = [FlagEnumDocs];
}

def EnumExtensibility : InheritableAttr {
  let Spellings = [Clang<"enum_extensibility">];
  let Subjects = SubjectList<[Enum]>;
  let Args = [EnumArgument<"Extensibility", "Kind",
              ["closed", "open"], ["Closed", "Open"]>];
  let Documentation = [EnumExtensibilityDocs];
}

def Flatten : InheritableAttr {
  let Spellings = [GCC<"flatten">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [FlattenDocs];
}

def Format : InheritableAttr {
  let Spellings = [GCC<"format">];
  let Args = [IdentifierArgument<"Type">, IntArgument<"FormatIdx">,
              IntArgument<"FirstArg">];
  let Subjects = SubjectList<[ObjCMethod, Block, HasFunctionProto]>;
  let Documentation = [FormatDocs];
}

def FormatArg : InheritableAttr {
  let Spellings = [GCC<"format_arg">];
  let Args = [ParamIdxArgument<"FormatIdx">];
  let Subjects = SubjectList<[ObjCMethod, HasFunctionProto]>;
  let Documentation = [Undocumented];
}

def GNUInline : InheritableAttr {
  let Spellings = [GCC<"gnu_inline">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [GnuInlineDocs];
}

def Hot : InheritableAttr {
  let Spellings = [GCC<"hot">];
  let Subjects = SubjectList<[Function]>;
  // An AST node is created for this attribute, but not actually used beyond
  // semantic checking for mutual exclusion with the Cold attribute.
  let Documentation = [Undocumented];
}

def IBAction : InheritableAttr {
  let Spellings = [Clang<"ibaction">];
  let Subjects = SubjectList<[ObjCInstanceMethod]>;
  // An AST node is created for this attribute, but is not used by other parts
  // of the compiler. However, this node needs to exist in the AST because
  // external tools rely on it.
  let Documentation = [Undocumented];
}

def IBOutlet : InheritableAttr {
  let Spellings = [Clang<"iboutlet">];
//  let Subjects = [ObjCIvar, ObjCProperty];
  let Documentation = [Undocumented];
}

def IBOutletCollection : InheritableAttr {
  let Spellings = [Clang<"iboutletcollection">];
  let Args = [TypeArgument<"Interface", 1>];
//  let Subjects = [ObjCIvar, ObjCProperty];
  let Documentation = [Undocumented];
}

def IFunc : Attr, TargetSpecificAttr<TargetELF> {
  let Spellings = [GCC<"ifunc">];
  let Args = [StringArgument<"Resolver">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [IFuncDocs];
}

def Restrict : InheritableAttr {
  let Spellings = [Declspec<"restrict">, GCC<"malloc">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def LayoutVersion : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"layout_version">];
  let Args = [UnsignedArgument<"Version">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [LayoutVersionDocs];
}

def LifetimeBound : DeclOrTypeAttr {
  let Spellings = [Clang<"lifetimebound", 0>];
  let Subjects = SubjectList<[ParmVar, ImplicitObjectParameter], ErrorDiag>;
  let Documentation = [LifetimeBoundDocs];
  let LangOpts = [CPlusPlus];
}

def TrivialABI : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it requires the
  // CPlusPlus language option.
  let Spellings = [Clang<"trivial_abi", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [TrivialABIDocs];
  let LangOpts = [CPlusPlus];
}

def MaxFieldAlignment : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [UnsignedArgument<"Alignment">];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def MayAlias : InheritableAttr {
  // FIXME: this is a type attribute in GCC, but a declaration attribute here.
  let Spellings = [GCC<"may_alias">];
  let Documentation = [Undocumented];
}

def MSABI : DeclOrTypeAttr {
  let Spellings = [GCC<"ms_abi">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [MSABIDocs];
}

def MSP430Interrupt : InheritableAttr, TargetSpecificAttr<TargetMSP430> {
  // NOTE: If you add any additional spellings, ARMInterrupt's, MipsInterrupt's
  // and AnyX86Interrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Args = [UnsignedArgument<"Number">];
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def Mips16 : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"mips16">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def MipsInterrupt : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  // NOTE: If you add any additional spellings, ARMInterrupt's,
  // MSP430Interrupt's and AnyX86Interrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let Args = [EnumArgument<"Interrupt", "InterruptType",
                           ["vector=sw0", "vector=sw1", "vector=hw0",
                            "vector=hw1", "vector=hw2", "vector=hw3",
                            "vector=hw4", "vector=hw5", "eic", ""],
                           ["sw0", "sw1", "hw0", "hw1", "hw2", "hw3",
                            "hw4", "hw5", "eic", "eic"]
                           >];
  let ParseKind = "Interrupt";
  let Documentation = [MipsInterruptDocs];
}

def MicroMips : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"micromips">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MicroMipsDocs];
}

def MipsLongCall : InheritableAttr, TargetSpecificAttr<TargetAnyMips> {
  let Spellings = [GCC<"long_call">, GCC<"far">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [MipsLongCallStyleDocs];
}

def MipsShortCall : InheritableAttr, TargetSpecificAttr<TargetAnyMips> {
  let Spellings = [GCC<"short_call">, GCC<"near">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [MipsShortCallStyleDocs];
}

def Mode : Attr {
  let Spellings = [GCC<"mode">];
  let Subjects = SubjectList<[Var, Enum, TypedefName, Field], ErrorDiag>;
  let Args = [IdentifierArgument<"Mode">];
  let Documentation = [Undocumented];
  // This is notionally a type attribute, which #pragma clang attribute
  // generally does not support.
  let PragmaAttributeSupport = 0;
}

def Naked : InheritableAttr {
  let Spellings = [GCC<"naked">, Declspec<"naked">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def NeonPolyVectorType : TypeAttr {
  let Spellings = [Clang<"neon_polyvector_type">];
  let Args = [IntArgument<"NumElements">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def NeonVectorType : TypeAttr {
  let Spellings = [Clang<"neon_vector_type">];
  let Args = [IntArgument<"NumElements">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def ReturnsTwice : InheritableAttr {
  let Spellings = [GCC<"returns_twice">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def DisableTailCalls : InheritableAttr {
  let Spellings = [Clang<"disable_tail_calls">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Documentation = [DisableTailCallsDocs];
}

def NoAlias : InheritableAttr {
  let Spellings = [Declspec<"noalias">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoAliasDocs];
}

def NoCommon : InheritableAttr {
  let Spellings = [GCC<"nocommon">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [Undocumented];
}

def NoDebug : InheritableAttr {
  let Spellings = [GCC<"nodebug">];
  let Subjects = SubjectList<[FunctionLike, ObjCMethod, NonParmVar]>;
  let Documentation = [NoDebugDocs];
}

def NoDuplicate : InheritableAttr {
  let Spellings = [Clang<"noduplicate">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoDuplicateDocs];
}

def Convergent : InheritableAttr {
  let Spellings = [Clang<"convergent">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [ConvergentDocs];
}

def NoInline : InheritableAttr {
  let Spellings = [GCC<"noinline">, Declspec<"noinline">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def NoMips16 : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"nomips16">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def NoMicroMips : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"nomicromips">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MicroMipsDocs];
}

def RISCVInterrupt : InheritableAttr, TargetSpecificAttr<TargetRISCV> {
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let Args = [EnumArgument<"Interrupt", "InterruptType",
                           ["user", "supervisor", "machine"],
                           ["user", "supervisor", "machine"],
                           1>];
  let ParseKind = "Interrupt";
  let Documentation = [RISCVInterruptDocs];
}

// This is not a TargetSpecificAttr so that is silently accepted and
// ignored on other targets as encouraged by the OpenCL spec.
//
// See OpenCL 1.2 6.11.5: "It is our intention that a particular
// implementation of OpenCL be free to ignore all attributes and the
// resulting executable binary will produce the same result."
//
// However, only AMD GPU targets will emit the corresponding IR
// attribute.
//
// FIXME: This provides a sub-optimal error message if you attempt to
// use this in CUDA, since CUDA does not use the same terminology.
//
// FIXME: SubjectList should be for OpenCLKernelFunction, but is not to
// workaround needing to see kernel attribute before others to know if
// this should be rejected on non-kernels.

def AMDGPUFlatWorkGroupSize : InheritableAttr {
  let Spellings = [Clang<"amdgpu_flat_work_group_size", 0>];
  let Args = [UnsignedArgument<"Min">, UnsignedArgument<"Max">];
  let Documentation = [AMDGPUFlatWorkGroupSizeDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUWavesPerEU : InheritableAttr {
  let Spellings = [Clang<"amdgpu_waves_per_eu", 0>];
  let Args = [UnsignedArgument<"Min">, UnsignedArgument<"Max", 1>];
  let Documentation = [AMDGPUWavesPerEUDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUNumSGPR : InheritableAttr {
  let Spellings = [Clang<"amdgpu_num_sgpr", 0>];
  let Args = [UnsignedArgument<"NumSGPR">];
  let Documentation = [AMDGPUNumSGPRNumVGPRDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUNumVGPR : InheritableAttr {
  let Spellings = [Clang<"amdgpu_num_vgpr", 0>];
  let Args = [UnsignedArgument<"NumVGPR">];
  let Documentation = [AMDGPUNumSGPRNumVGPRDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def WebAssemblyImportModule : InheritableAttr,
                              TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [Clang<"import_module">];
  let Args = [StringArgument<"ImportModule">];
  let Documentation = [WebAssemblyImportModuleDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
}

def WebAssemblyImportName : InheritableAttr,
                            TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [Clang<"import_name">];
  let Args = [StringArgument<"ImportName">];
  let Documentation = [WebAssemblyImportNameDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
}

def NoSplitStack : InheritableAttr {
  let Spellings = [GCC<"no_split_stack">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [NoSplitStackDocs];
}

def NonNull : InheritableParamAttr {
  let Spellings = [GCC<"nonnull">];
  let Subjects = SubjectList<[ObjCMethod, HasFunctionProto, ParmVar], WarnDiag,
                             "functions, methods, and parameters">;
  let Args = [VariadicParamIdxArgument<"Args">];
  let AdditionalMembers = [{
    bool isNonNull(unsigned IdxAST) const {
      if (!args_size())
        return true;
      return args_end() != std::find_if(
          args_begin(), args_end(),
          [=](const ParamIdx &Idx) { return Idx.getASTIndex() == IdxAST; });
    }
  }];
  // FIXME: We should merge duplicates into a single nonnull attribute.
  let InheritEvenIfAlreadyPresent = 1;
  let Documentation = [NonNullDocs];
}

def ReturnsNonNull : InheritableAttr {
  let Spellings = [GCC<"returns_nonnull">];
  let Subjects = SubjectList<[ObjCMethod, Function]>;
  let Documentation = [ReturnsNonNullDocs];
}

// pass_object_size(N) indicates that the parameter should have
// __builtin_object_size with Type=N evaluated on the parameter at the callsite.
def PassObjectSize : InheritableParamAttr {
  let Spellings = [Clang<"pass_object_size">];
  let Args = [IntArgument<"Type">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [PassObjectSizeDocs];
}

// Nullability type attributes.
def TypeNonNull : TypeAttr {
  let Spellings = [Keyword<"_Nonnull">];
  let Documentation = [TypeNonNullDocs];
}

def TypeNullable : TypeAttr {
  let Spellings = [Keyword<"_Nullable">];
  let Documentation = [TypeNullableDocs];
}

def TypeNullUnspecified : TypeAttr {
  let Spellings = [Keyword<"_Null_unspecified">];
  let Documentation = [TypeNullUnspecifiedDocs];
}

// This is a marker used to indicate that an __unsafe_unretained qualifier was
// ignored because ARC is not enabled. The usual representation for this
// qualifier is as an ObjCOwnership attribute with Kind == "none".
def ObjCInertUnsafeUnretained : TypeAttr {
  let Spellings = [Keyword<"__unsafe_unretained">];
  let Documentation = [Undocumented];
}

def ObjCKindOf : TypeAttr {
  let Spellings = [Keyword<"__kindof">];
  let Documentation = [Undocumented];
}

def NoEscape : Attr {
  let Spellings = [Clang<"noescape">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [NoEscapeDocs];
}

def AssumeAligned : InheritableAttr {
  let Spellings = [GCC<"assume_aligned">];
  let Subjects = SubjectList<[ObjCMethod, Function]>;
  let Args = [ExprArgument<"Alignment">, ExprArgument<"Offset", 1>];
  let Documentation = [AssumeAlignedDocs];
}

def AllocAlign : InheritableAttr {
  let Spellings = [GCC<"alloc_align">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let Args = [ParamIdxArgument<"ParamIndex">];
  let Documentation = [AllocAlignDocs];
}

def NoReturn : InheritableAttr {
  let Spellings = [GCC<"noreturn">, Declspec<"noreturn">];
  // FIXME: Does GCC allow this on the function instead?
  let Documentation = [Undocumented];
}

def NoInstrumentFunction : InheritableAttr {
  let Spellings = [GCC<"no_instrument_function">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def NotTailCalled : InheritableAttr {
  let Spellings = [Clang<"not_tail_called">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NotTailCalledDocs];
}

def NoStackProtector : InheritableAttr {
  let Spellings = [Clang<"no_stack_protector">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoStackProtectorDocs];
}

def NoThrow : InheritableAttr {
  let Spellings = [GCC<"nothrow">, Declspec<"nothrow">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoThrowDocs];
}

def NvWeak : IgnoredAttr {
  // No Declspec spelling of this attribute; the CUDA headers use
  // __attribute__((nv_weak)) unconditionally. Does not receive an [[]]
  // spelling because it is a CUDA attribute.
  let Spellings = [GNU<"nv_weak">];
  let LangOpts = [CUDA];
}

def ObjCBridge : InheritableAttr {
  let Spellings = [Clang<"objc_bridge">];
  let Subjects = SubjectList<[Record, TypedefName], ErrorDiag>;
  let Args = [IdentifierArgument<"BridgedType">];
  let Documentation = [Undocumented];
}

def ObjCBridgeMutable : InheritableAttr {
  let Spellings = [Clang<"objc_bridge_mutable">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Args = [IdentifierArgument<"BridgedType">];
  let Documentation = [Undocumented];
}

def ObjCBridgeRelated : InheritableAttr {
  let Spellings = [Clang<"objc_bridge_related">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Args = [IdentifierArgument<"RelatedClass">,
          IdentifierArgument<"ClassMethod">,
          IdentifierArgument<"InstanceMethod">];
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def NSReturnsRetained : DeclOrTypeAttr {
  let Spellings = [Clang<"ns_returns_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"ns_returns_not_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSReturnsAutoreleased : InheritableAttr {
  let Spellings = [Clang<"ns_returns_autoreleased">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSConsumesSelf : InheritableAttr {
  let Spellings = [Clang<"ns_consumes_self">];
  let Subjects = SubjectList<[ObjCMethod]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSConsumed : InheritableParamAttr {
  let Spellings = [Clang<"ns_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def ObjCException : InheritableAttr {
  let Spellings = [Clang<"objc_exception">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCMethodFamily : InheritableAttr {
  let Spellings = [Clang<"objc_method_family">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Args = [EnumArgument<"Family", "FamilyKind",
               ["none", "alloc", "copy", "init", "mutableCopy", "new"],
               ["OMF_None", "OMF_alloc", "OMF_copy", "OMF_init",
                "OMF_mutableCopy", "OMF_new"]>];
  let Documentation = [ObjCMethodFamilyDocs];
}

def ObjCNSObject : InheritableAttr {
  let Spellings = [Clang<"NSObject">];
  let Documentation = [Undocumented];
}

def ObjCIndependentClass : InheritableAttr {
  let Spellings = [Clang<"objc_independent_class">];
  let Documentation = [Undocumented];
}

def ObjCPreciseLifetime : InheritableAttr {
  let Spellings = [Clang<"objc_precise_lifetime">];
  let Subjects = SubjectList<[Var], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCReturnsInnerPointer : InheritableAttr {
  let Spellings = [Clang<"objc_returns_inner_pointer">];
  let Subjects = SubjectList<[ObjCMethod, ObjCProperty], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCRequiresSuper : InheritableAttr {
  let Spellings = [Clang<"objc_requires_super">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Documentation = [ObjCRequiresSuperDocs];
}

def ObjCRootClass : InheritableAttr {
  let Spellings = [Clang<"objc_root_class">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCSubclassingRestricted : InheritableAttr {
  let Spellings = [Clang<"objc_subclassing_restricted">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [ObjCSubclassingRestrictedDocs];
}

def ObjCExplicitProtocolImpl : InheritableAttr {
  let Spellings = [Clang<"objc_protocol_requires_explicit_implementation">];
  let Subjects = SubjectList<[ObjCProtocol], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCDesignatedInitializer : Attr {
  let Spellings = [Clang<"objc_designated_initializer">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCRuntimeName : Attr {
  let Spellings = [Clang<"objc_runtime_name">];
  let Subjects = SubjectList<[ObjCInterface, ObjCProtocol], ErrorDiag>;
  let Args = [StringArgument<"MetadataName">];
  let Documentation = [ObjCRuntimeNameDocs];
}

def ObjCRuntimeVisible : Attr {
  let Spellings = [Clang<"objc_runtime_visible">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [ObjCRuntimeVisibleDocs];
}

def ObjCBoxable : Attr {
  let Spellings = [Clang<"objc_boxable">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Documentation = [ObjCBoxableDocs];
}

def OptimizeNone : InheritableAttr {
  let Spellings = [Clang<"optnone">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Documentation = [OptnoneDocs];
}

def Overloadable : Attr {
  let Spellings = [Clang<"overloadable">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [OverloadableDocs];
}

def Override : InheritableAttr {
  let Spellings = [Keyword<"override">];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def Ownership : InheritableAttr {
  let Spellings = [Clang<"ownership_holds">, Clang<"ownership_returns">,
                   Clang<"ownership_takes">];
  let Accessors = [Accessor<"isHolds", [Clang<"ownership_holds">]>,
                   Accessor<"isReturns", [Clang<"ownership_returns">]>,
                   Accessor<"isTakes", [Clang<"ownership_takes">]>];
  let AdditionalMembers = [{
    enum OwnershipKind { Holds, Returns, Takes };
    OwnershipKind getOwnKind() const {
      return isHolds() ? Holds :
             isTakes() ? Takes :
             Returns;
    }
  }];
  let Args = [IdentifierArgument<"Module">,
              VariadicParamIdxArgument<"Args">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let Documentation = [Undocumented];
}

def Packed : InheritableAttr {
  let Spellings = [GCC<"packed">];
//  let Subjects = [Tag, Field];
  let Documentation = [Undocumented];
}

def IntelOclBicc : DeclOrTypeAttr {
  let Spellings = [Clang<"intel_ocl_bicc", 0>];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

def Pcs : DeclOrTypeAttr {
  let Spellings = [GCC<"pcs">];
  let Args = [EnumArgument<"PCS", "PCSType",
                           ["aapcs", "aapcs-vfp"],
                           ["AAPCS", "AAPCS_VFP"]>];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [PcsDocs];
}

def AArch64VectorPcs: DeclOrTypeAttr {
  let Spellings = [Clang<"aarch64_vector_pcs">];
  let Documentation = [AArch64VectorPcsDocs];
}

def Pure : InheritableAttr {
  let Spellings = [GCC<"pure">];
  let Documentation = [Undocumented];
}

def Regparm : TypeAttr {
  let Spellings = [GCC<"regparm">];
  let Args = [UnsignedArgument<"NumParams">];
  let Documentation = [RegparmDocs];
  // Represented as part of the enclosing function type.
  let ASTNode = 0;
}

def NoDeref : TypeAttr {
  let Spellings = [Clang<"noderef">];
  let Documentation = [NoDerefDocs];
}

def ReqdWorkGroupSize : InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"reqd_work_group_size">];
  let Args = [UnsignedArgument<"XDim">, UnsignedArgument<"YDim">,
              UnsignedArgument<"ZDim">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def RequireConstantInit : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it requires the
  // CPlusPlus language option.
  let Spellings = [Clang<"require_constant_initialization", 0>];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [RequireConstantInitDocs];
  let LangOpts = [CPlusPlus];
}

def WorkGroupSizeHint :  InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"work_group_size_hint">];
  let Args = [UnsignedArgument<"XDim">,
              UnsignedArgument<"YDim">,
              UnsignedArgument<"ZDim">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def InitPriority : InheritableAttr {
  let Spellings = [GCC<"init_priority">];
  let Args = [UnsignedArgument<"Priority">];
  let Subjects = SubjectList<[Var], ErrorDiag>;
  let Documentation = [Undocumented];
}

def Section : InheritableAttr {
  let Spellings = [GCC<"section">, Declspec<"allocate">];
  let Args = [StringArgument<"Name">];
  let Subjects =
      SubjectList<[ Function, GlobalVar, ObjCMethod, ObjCProperty ], ErrorDiag>;
  let Documentation = [SectionDocs];
}

def CodeSeg : InheritableAttr {
  let Spellings = [Declspec<"code_seg">];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[Function, CXXRecord], ErrorDiag>;
  let Documentation = [CodeSegDocs];
}

def PragmaClangBSSSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [Undocumented];
}

def PragmaClangDataSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [Undocumented];
}

def PragmaClangRodataSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [Undocumented];
}

def PragmaClangTextSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def Sentinel : InheritableAttr {
  let Spellings = [GCC<"sentinel">];
  let Args = [DefaultIntArgument<"Sentinel", 0>,
              DefaultIntArgument<"NullPos", 0>];
//  let Subjects = SubjectList<[Function, ObjCMethod, Block, Var]>;
  let Documentation = [Undocumented];
}

def StdCall : DeclOrTypeAttr {
  let Spellings = [GCC<"stdcall">, Keyword<"__stdcall">, Keyword<"_stdcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [StdCallDocs];
}

def SwiftCall : DeclOrTypeAttr {
  let Spellings = [Clang<"swiftcall">];
//  let Subjects = SubjectList<[Function]>;
  let Documentation = [SwiftCallDocs];
}

def SwiftContext : ParameterABIAttr {
  let Spellings = [Clang<"swift_context">];
  let Documentation = [SwiftContextDocs];
}

def SwiftErrorResult : ParameterABIAttr {
  let Spellings = [Clang<"swift_error_result">];
  let Documentation = [SwiftErrorResultDocs];
}

def SwiftIndirectResult : ParameterABIAttr {
  let Spellings = [Clang<"swift_indirect_result">];
  let Documentation = [SwiftIndirectResultDocs];
}

def Suppress : StmtAttr {
  let Spellings = [CXX11<"gsl", "suppress">];
  let Args = [VariadicStringArgument<"DiagnosticIdentifiers">];
  let Documentation = [SuppressDocs];
}

def SysVABI : DeclOrTypeAttr {
  let Spellings = [GCC<"sysv_abi">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

def ThisCall : DeclOrTypeAttr {
  let Spellings = [GCC<"thiscall">, Keyword<"__thiscall">,
                   Keyword<"_thiscall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [ThisCallDocs];
}

def VectorCall : DeclOrTypeAttr {
  let Spellings = [Clang<"vectorcall">, Keyword<"__vectorcall">,
                   Keyword<"_vectorcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [VectorCallDocs];
}

def Pascal : DeclOrTypeAttr {
  let Spellings = [Clang<"pascal">, Keyword<"__pascal">, Keyword<"_pascal">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

def PreserveMost : DeclOrTypeAttr {
  let Spellings = [Clang<"preserve_most">];
  let Documentation = [PreserveMostDocs];
}

def PreserveAll : DeclOrTypeAttr {
  let Spellings = [Clang<"preserve_all">];
  let Documentation = [PreserveAllDocs];
}

def Target : InheritableAttr {
  let Spellings = [GCC<"target">];
  let Args = [StringArgument<"featuresStr">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [TargetDocs];
  let AdditionalMembers = [{
    struct ParsedTargetAttr {
      std::vector<std::string> Features;
      StringRef Architecture;
      bool DuplicateArchitecture = false;
      bool operator ==(const ParsedTargetAttr &Other) const {
        return DuplicateArchitecture == Other.DuplicateArchitecture &&
               Architecture == Other.Architecture && Features == Other.Features;
      }
    };
    ParsedTargetAttr parse() const {
      return parse(getFeaturesStr());
    }

    StringRef getArchitecture() const {
      StringRef Features = getFeaturesStr();
      if (Features == "default") return {};

      SmallVector<StringRef, 1> AttrFeatures;
      Features.split(AttrFeatures, ",");

      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();
        if (Feature.startswith("arch="))
          return Feature.drop_front(sizeof("arch=") - 1);
      }
      return "";
    }

    // Gets the list of features as simple string-refs with no +/- or 'no-'.
    // Only adds the items to 'Out' that are additions.
    void getAddedFeatures(llvm::SmallVectorImpl<StringRef> &Out) const {
      StringRef Features = getFeaturesStr();
      if (Features == "default") return;

      SmallVector<StringRef, 1> AttrFeatures;
      Features.split(AttrFeatures, ",");

      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();

        if (!Feature.startswith("no-") && !Feature.startswith("arch=") &&
            !Feature.startswith("fpmath=") && !Feature.startswith("tune="))
          Out.push_back(Feature);
      }
    }

    template<class Compare>
    ParsedTargetAttr parse(Compare cmp) const {
      ParsedTargetAttr Attrs = parse();
      llvm::sort(std::begin(Attrs.Features), std::end(Attrs.Features), cmp);
      return Attrs;
    }

    bool isDefaultVersion() const { return getFeaturesStr() == "default"; }

    static ParsedTargetAttr parse(StringRef Features) {
      ParsedTargetAttr Ret;
      if (Features == "default") return Ret;
      SmallVector<StringRef, 1> AttrFeatures;
      Features.split(AttrFeatures, ",");

      // Grab the various features and prepend a "+" to turn on the feature to
      // the backend and add them to our existing set of features.
      for (auto &Feature : AttrFeatures) {
        // Go ahead and trim whitespace rather than either erroring or
        // accepting it weirdly.
        Feature = Feature.trim();

        // We don't support cpu tuning this way currently.
        // TODO: Support the fpmath option. It will require checking
        // overall feature validity for the function with the rest of the
        // attributes on the function.
        if (Feature.startswith("fpmath=") || Feature.startswith("tune="))
          continue;

        // While we're here iterating check for a different target cpu.
        if (Feature.startswith("arch=")) {
          if (!Ret.Architecture.empty())
            Ret.DuplicateArchitecture = true;
          else
            Ret.Architecture = Feature.split("=").second.trim();
        } else if (Feature.startswith("no-"))
          Ret.Features.push_back("-" + Feature.split("-").second.str());
        else
          Ret.Features.push_back("+" + Feature.str());
      }
      return Ret;
    }
  }];
}

def MinVectorWidth : InheritableAttr {
  let Spellings = [Clang<"min_vector_width">];
  let Args = [UnsignedArgument<"VectorWidth">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MinVectorWidthDocs];
}

def TransparentUnion : InheritableAttr {
  let Spellings = [GCC<"transparent_union">];
//  let Subjects = SubjectList<[Record, TypedefName]>;
  let Documentation = [TransparentUnionDocs];
  let LangOpts = [COnly];
}

def Unavailable : InheritableAttr {
  let Spellings = [Clang<"unavailable">];
  let Args = [StringArgument<"Message", 1>,
              EnumArgument<"ImplicitReason", "ImplicitReason",
                ["", "", "", ""],
                ["IR_None",
                 "IR_ARCForbiddenType",
                 "IR_ForbiddenWeak",
                 "IR_ARCForbiddenConversion",
                 "IR_ARCInitReturnsUnrelated",
                 "IR_ARCFieldWithOwnership"], 1, /*fake*/ 1>];
  let Documentation = [Undocumented];
}

def DiagnoseIf : InheritableAttr {
  // Does not have a [[]] spelling because this attribute requires the ability
  // to parse function arguments but the attribute is not written in the type
  // position.
  let Spellings = [GNU<"diagnose_if">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty]>;
  let Args = [ExprArgument<"Cond">, StringArgument<"Message">,
              EnumArgument<"DiagnosticType",
                           "DiagnosticType",
                           ["error", "warning"],
                           ["DT_Error", "DT_Warning"]>,
              BoolArgument<"ArgDependent", 0, /*fake*/ 1>,
              NamedArgument<"Parent", 0, /*fake*/ 1>];
  let InheritEvenIfAlreadyPresent = 1;
  let LateParsed = 1;
  let AdditionalMembers = [{
    bool isError() const { return diagnosticType == DT_Error; }
    bool isWarning() const { return diagnosticType == DT_Warning; }
  }];
  let TemplateDependent = 1;
  let Documentation = [DiagnoseIfDocs];
}

def ArcWeakrefUnavailable : InheritableAttr {
  let Spellings = [Clang<"objc_arc_weak_reference_unavailable">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCGC : TypeAttr {
  let Spellings = [Clang<"objc_gc">];
  let Args = [IdentifierArgument<"Kind">];
  let Documentation = [Undocumented];
}

def ObjCOwnership : DeclOrTypeAttr {
  let Spellings = [Clang<"objc_ownership">];
  let Args = [IdentifierArgument<"Kind">];
  let Documentation = [Undocumented];
}

def ObjCRequiresPropertyDefs : InheritableAttr {
  let Spellings = [Clang<"objc_requires_property_definitions">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
}

def Unused : InheritableAttr {
  let Spellings = [CXX11<"", "maybe_unused", 201603>, GCC<"unused">,
                   C2x<"", "maybe_unused">];
  let Subjects = SubjectList<[Var, ObjCIvar, Type, Enum, EnumConstant, Label,
                              Field, ObjCMethod, FunctionLike]>;
  let Documentation = [WarnMaybeUnusedDocs];
}

def Used : InheritableAttr {
  let Spellings = [GCC<"used">];
  let Subjects = SubjectList<[NonLocalVar, Function, ObjCMethod]>;
  let Documentation = [Undocumented];
}

def Uuid : InheritableAttr {
  let Spellings = [Declspec<"uuid">, Microsoft<"uuid">];
  let Args = [StringArgument<"Guid">];
  let Subjects = SubjectList<[Record, Enum]>;
  // FIXME: Allow expressing logical AND for LangOpts. Our condition should be:
  // CPlusPlus && (MicrosoftExt || Borland)
  let LangOpts = [MicrosoftExt, Borland];
  let Documentation = [Undocumented];
}

def VectorSize : TypeAttr {
  let Spellings = [GCC<"vector_size">];
  let Args = [ExprArgument<"NumBytes">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def VecTypeHint : InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"vec_type_hint">];
  let Args = [TypeArgument<"TypeHint">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def Visibility : InheritableAttr {
  let Clone = 0;
  let Spellings = [GCC<"visibility">];
  let Args = [EnumArgument<"Visibility", "VisibilityType",
                           ["default", "hidden", "internal", "protected"],
                           ["Default", "Hidden", "Hidden", "Protected"]>];
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [Undocumented];
}

def TypeVisibility : InheritableAttr {
  let Clone = 0;
  let Spellings = [Clang<"type_visibility">];
  let Args = [EnumArgument<"Visibility", "VisibilityType",
                           ["default", "hidden", "internal", "protected"],
                           ["Default", "Hidden", "Hidden", "Protected"]>];
//  let Subjects = [Tag, ObjCInterface, Namespace];
  let Documentation = [Undocumented];
}

def VecReturn : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"vecreturn", 0>];
  let Subjects = SubjectList<[CXXRecord], ErrorDiag>;
  let Documentation = [Undocumented];
}

def WarnUnused : InheritableAttr {
  let Spellings = [GCC<"warn_unused">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
}

def WarnUnusedResult : InheritableAttr {
  let Spellings = [CXX11<"", "nodiscard", 201603>, C2x<"", "nodiscard">,
                   CXX11<"clang", "warn_unused_result">,
                   GCC<"warn_unused_result">];
  let Subjects = SubjectList<[ObjCMethod, Enum, Record, FunctionLike]>;
  let Documentation = [WarnUnusedResultsDocs];
}

def Weak : InheritableAttr {
  let Spellings = [GCC<"weak">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [Undocumented];
}

def WeakImport : InheritableAttr {
  let Spellings = [Clang<"weak_import">];
  let Documentation = [Undocumented];
}

def WeakRef : InheritableAttr {
  let Spellings = [GCC<"weakref">];
  // A WeakRef that has an argument is treated as being an AliasAttr
  let Args = [StringArgument<"Aliasee", 1>];
  let Subjects = SubjectList<[Var, Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def LTOVisibilityPublic : InheritableAttr {
  let Spellings = [Clang<"lto_visibility_public">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [LTOVisibilityDocs];
}

def AnyX86Interrupt : InheritableAttr, TargetSpecificAttr<TargetAnyX86> {
  // NOTE: If you add any additional spellings, ARMInterrupt's,
  // MSP430Interrupt's and MipsInterrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def AnyX86NoCallerSavedRegisters : InheritableAttr,
                                   TargetSpecificAttr<TargetAnyX86> {
  let Spellings = [GCC<"no_caller_saved_registers">];
  let Documentation = [AnyX86NoCallerSavedRegistersDocs];
}

def AnyX86NoCfCheck : DeclOrTypeAttr, TargetSpecificAttr<TargetAnyX86>{
  let Spellings = [GCC<"nocf_check">];
  let Subjects = SubjectList<[FunctionLike]>;
  let Documentation = [AnyX86NoCfCheckDocs];
}

def X86ForceAlignArgPointer : InheritableAttr, TargetSpecificAttr<TargetAnyX86> {
  let Spellings = [GCC<"force_align_arg_pointer">];
  // Technically, this appertains to a FunctionDecl, but the target-specific
  // code silently allows anything function-like (such as typedefs or function
  // pointers), but does not apply the attribute to them.
  let Documentation = [X86ForceAlignArgPointerDocs];
}

def NoSanitize : InheritableAttr {
  let Spellings = [Clang<"no_sanitize">];
  let Args = [VariadicStringArgument<"Sanitizers">];
  let Subjects = SubjectList<[Function, ObjCMethod, GlobalVar], ErrorDiag>;
  let Documentation = [NoSanitizeDocs];
  let AdditionalMembers = [{
    SanitizerMask getMask() const {
      SanitizerMask Mask = 0;
      for (auto SanitizerName : sanitizers()) {
        SanitizerMask ParsedMask =
            parseSanitizerValue(SanitizerName, /*AllowGroups=*/true);
        Mask |= expandSanitizerGroups(ParsedMask);
      }
      return Mask;
    }
  }];
}

// Attributes to disable a specific sanitizer. No new sanitizers should be added
// to this list; the no_sanitize attribute should be extended instead.
def NoSanitizeSpecific : InheritableAttr {
  let Spellings = [GCC<"no_address_safety_analysis">,
                   GCC<"no_sanitize_address">,
                   GCC<"no_sanitize_thread">,
                   Clang<"no_sanitize_memory">];
  let Subjects = SubjectList<[Function, GlobalVar], ErrorDiag>;
  let Documentation = [NoSanitizeAddressDocs, NoSanitizeThreadDocs,
                       NoSanitizeMemoryDocs];
  let ASTNode = 0;
}

// C/C++ Thread safety attributes (e.g. for deadlock, data race checking)
// Not all of these attributes will be given a [[]] spelling. The attributes
// which require access to function parameter names cannot use the [[]] spelling
// because they are not written in the type position. Some attributes are given
// an updated captability-based name and the older name will only be supported
// under the GNU-style spelling.
def GuardedVar : InheritableAttr {
  let Spellings = [Clang<"guarded_var", 0>];
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def PtGuardedVar : InheritableAttr {
  let Spellings = [Clang<"pt_guarded_var", 0>];
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def Lockable : InheritableAttr {
  let Spellings = [GNU<"lockable">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
  let ASTNode = 0;  // Replaced by Capability
}

def ScopedLockable : InheritableAttr {
  let Spellings = [Clang<"scoped_lockable", 0>];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
}

def Capability : InheritableAttr {
  let Spellings = [Clang<"capability", 0>, Clang<"shared_capability", 0>];
  let Subjects = SubjectList<[Record, TypedefName], ErrorDiag>;
  let Args = [StringArgument<"Name">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"shared_capability", 0>]>];
  let Documentation = [Undocumented];
  let AdditionalMembers = [{
    bool isMutex() const { return getName().equals_lower("mutex"); }
    bool isRole() const { return getName().equals_lower("role"); }
  }];
}

def AssertCapability : InheritableAttr {
  let Spellings = [Clang<"assert_capability", 0>,
                   Clang<"assert_shared_capability", 0>];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"assert_shared_capability", 0>]>];
  let Documentation = [AssertCapabilityDocs];
}

def AcquireCapability : InheritableAttr {
  let Spellings = [Clang<"acquire_capability", 0>,
                   Clang<"acquire_shared_capability", 0>,
                   GNU<"exclusive_lock_function">,
                   GNU<"shared_lock_function">];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"acquire_shared_capability", 0>,
                     GNU<"shared_lock_function">]>];
  let Documentation = [AcquireCapabilityDocs];
}

def TryAcquireCapability : InheritableAttr {
  let Spellings = [Clang<"try_acquire_capability", 0>,
                   Clang<"try_acquire_shared_capability", 0>];
  let Subjects = SubjectList<[Function],
                             ErrorDiag>;
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"try_acquire_shared_capability", 0>]>];
  let Documentation = [TryAcquireCapabilityDocs];
}

def ReleaseCapability : InheritableAttr {
  let Spellings = [Clang<"release_capability", 0>,
                   Clang<"release_shared_capability", 0>,
                   Clang<"release_generic_capability", 0>,
                   Clang<"unlock_function", 0>];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"release_shared_capability", 0>]>,
                   Accessor<"isGeneric",
                     [Clang<"release_generic_capability", 0>,
                      Clang<"unlock_function", 0>]>];
  let Documentation = [ReleaseCapabilityDocs];
}

def RequiresCapability : InheritableAttr {
  let Spellings = [Clang<"requires_capability", 0>,
                   Clang<"exclusive_locks_required", 0>,
                   Clang<"requires_shared_capability", 0>,
                   Clang<"shared_locks_required", 0>];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Accessors = [Accessor<"isShared", [Clang<"requires_shared_capability", 0>,
                                         Clang<"shared_locks_required", 0>]>];
  let Documentation = [Undocumented];
}

def NoThreadSafetyAnalysis : InheritableAttr {
  let Spellings = [Clang<"no_thread_safety_analysis">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def GuardedBy : InheritableAttr {
  let Spellings = [GNU<"guarded_by">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def PtGuardedBy : InheritableAttr {
  let Spellings = [GNU<"pt_guarded_by">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AcquiredAfter : InheritableAttr {
  let Spellings = [GNU<"acquired_after">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AcquiredBefore : InheritableAttr {
  let Spellings = [GNU<"acquired_before">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AssertExclusiveLock : InheritableAttr {
  let Spellings = [GNU<"assert_exclusive_lock">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def AssertSharedLock : InheritableAttr {
  let Spellings = [GNU<"assert_shared_lock">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// The first argument is an integer or boolean value specifying the return value
// of a successful lock acquisition.
def ExclusiveTrylockFunction : InheritableAttr {
  let Spellings = [GNU<"exclusive_trylock_function">];
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// The first argument is an integer or boolean value specifying the return value
// of a successful lock acquisition.
def SharedTrylockFunction : InheritableAttr {
  let Spellings = [GNU<"shared_trylock_function">];
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def LockReturned : InheritableAttr {
  let Spellings = [GNU<"lock_returned">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def LocksExcluded : InheritableAttr {
  let Spellings = [GNU<"locks_excluded">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = 1;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// C/C++ consumed attributes.

def Consumable : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Args = [EnumArgument<"DefaultState", "ConsumedState",
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ConsumableDocs];
}

def ConsumableAutoCast : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable_auto_cast_state", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [Undocumented];
}

def ConsumableSetOnRead : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable_set_state_on_read", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [Undocumented];
}

def CallableWhen : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"callable_when", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [VariadicEnumArgument<"CallableStates", "ConsumedState",
                                   ["unknown", "consumed", "unconsumed"],
                                   ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [CallableWhenDocs];
}

def ParamTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to a parameter whose type is a consumable C++ class.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"param_typestate", 0>];
  let Subjects = SubjectList<[ParmVar]>;
  let Args = [EnumArgument<"ParamState", "ConsumedState",
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ParamTypestateDocs];
}

def ReturnTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to a parameter or function return type that is a consumable C++ class.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"return_typestate", 0>];
  let Subjects = SubjectList<[Function, ParmVar]>;
  let Args = [EnumArgument<"State", "ConsumedState",
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ReturnTypestateDocs];
}

def SetTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"set_typestate", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [EnumArgument<"NewState", "ConsumedState",
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [SetTypestateDocs];
}

def TestTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"test_typestate", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [EnumArgument<"TestState", "ConsumedState",
                           ["consumed", "unconsumed"],
                           ["Consumed", "Unconsumed"]>];
  let Documentation = [TestTypestateDocs];
}

// Type safety attributes for `void *' pointers and type tags.

def ArgumentWithTypeTag : InheritableAttr {
  let Spellings = [Clang<"argument_with_type_tag">,
                   Clang<"pointer_with_type_tag">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Args = [IdentifierArgument<"ArgumentKind">,
              ParamIdxArgument<"ArgumentIdx">,
              ParamIdxArgument<"TypeTagIdx">,
              BoolArgument<"IsPointer", /*opt*/0, /*fake*/1>];
  let Documentation = [ArgumentWithTypeTagDocs, PointerWithTypeTagDocs];
}

def TypeTagForDatatype : InheritableAttr {
  let Spellings = [Clang<"type_tag_for_datatype">];
  let Args = [IdentifierArgument<"ArgumentKind">,
              TypeArgument<"MatchingCType">,
              BoolArgument<"LayoutCompatible">,
              BoolArgument<"MustBeNull">];
//  let Subjects = SubjectList<[Var], ErrorDiag>;
  let HasCustomParsing = 1;
  let Documentation = [TypeTagForDatatypeDocs];
}

// Microsoft-related attributes

def MSNoVTable : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"novtable">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [MSNoVTableDocs];
}

def : IgnoredAttr {
  let Spellings = [Declspec<"property">];
}

def MSStruct : InheritableAttr {
  let Spellings = [GCC<"ms_struct">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
}

def DLLExport : InheritableAttr, TargetSpecificAttr<TargetWindows> {
  let Spellings = [Declspec<"dllexport">, GCC<"dllexport">];
  let Subjects = SubjectList<[Function, Var, CXXRecord, ObjCInterface]>;
  let Documentation = [DLLExportDocs];
}

def DLLExportStaticLocal : InheritableAttr, TargetSpecificAttr<TargetWindows> {
  // This attribute is used internally only when -fno-dllexport-inlines is
  // passed. This attribute is added to inline function of class having
  // dllexport attribute. And if the function has static local variables, this
  // attribute is used to whether the variables are exported or not. Also if
  // function has local static variables, the function is dllexported too.
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def DLLImport : InheritableAttr, TargetSpecificAttr<TargetWindows> {
  let Spellings = [Declspec<"dllimport">, GCC<"dllimport">];
  let Subjects = SubjectList<[Function, Var, CXXRecord, ObjCInterface]>;
  let Documentation = [DLLImportDocs];


  let AdditionalMembers = [{
private:
  bool PropagatedToBaseTemplate = false;

public:
  void setPropagatedToBaseTemplate() { PropagatedToBaseTemplate = true; }
  bool wasPropagatedToBaseTemplate() { return PropagatedToBaseTemplate; }
  }];
}

def DLLImportStaticLocal : InheritableAttr, TargetSpecificAttr<TargetWindows> {
  // This attribute is used internally only when -fno-dllexport-inlines is
  // passed. This attribute is added to inline function of class having
  // dllimport attribute. And if the function has static local variables, this
  // attribute is used to whether the variables are imported or not.
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def SelectAny : InheritableAttr {
  let Spellings = [Declspec<"selectany">, GCC<"selectany">];
  let Documentation = [SelectAnyDocs];
}

def Thread : Attr {
  let Spellings = [Declspec<"thread">];
  let LangOpts = [MicrosoftExt];
  let Documentation = [ThreadDocs];
  let Subjects = SubjectList<[Var]>;
}

def Win64 : IgnoredAttr {
  let Spellings = [Keyword<"__w64">];
  let LangOpts = [MicrosoftExt];
}

def Ptr32 : TypeAttr {
  let Spellings = [Keyword<"__ptr32">];
  let Documentation = [Undocumented];
}

def Ptr64 : TypeAttr {
  let Spellings = [Keyword<"__ptr64">];
  let Documentation = [Undocumented];
}

def SPtr : TypeAttr {
  let Spellings = [Keyword<"__sptr">];
  let Documentation = [Undocumented];
}

def UPtr : TypeAttr {
  let Spellings = [Keyword<"__uptr">];
  let Documentation = [Undocumented];
}

def MSInheritance : InheritableAttr {
  let LangOpts = [MicrosoftExt];
  let Args = [DefaultBoolArgument<"BestCase", /*default*/1, /*fake*/1>];
  let Spellings = [Keyword<"__single_inheritance">,
                   Keyword<"__multiple_inheritance">,
                   Keyword<"__virtual_inheritance">,
                   Keyword<"__unspecified_inheritance">];
  let AdditionalMembers = [{
  static bool hasVBPtrOffsetField(Spelling Inheritance) {
    return Inheritance == Keyword_unspecified_inheritance;
  }

  // Only member pointers to functions need a this adjustment, since it can be
  // combined with the field offset for data pointers.
  static bool hasNVOffsetField(bool IsMemberFunction, Spelling Inheritance) {
    return IsMemberFunction && Inheritance >= Keyword_multiple_inheritance;
  }

  static bool hasVBTableOffsetField(Spelling Inheritance) {
    return Inheritance >= Keyword_virtual_inheritance;
  }

  static bool hasOnlyOneField(bool IsMemberFunction,
                              Spelling Inheritance) {
    if (IsMemberFunction)
      return Inheritance <= Keyword_single_inheritance;
    return Inheritance <= Keyword_multiple_inheritance;
  }
  }];
  let Documentation = [MSInheritanceDocs];
}

def MSVtorDisp : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [UnsignedArgument<"vdm">];
  let SemaHandler = 0;

  let AdditionalMembers = [{
  enum Mode {
    Never,
    ForVBaseOverride,
    ForVFTable
  };

  Mode getVtorDispMode() const { return Mode(vdm); }
  }];
  let Documentation = [Undocumented];
}

def InitSeg : Attr {
  let Spellings = [Pragma<"", "init_seg">];
  let Args = [StringArgument<"Section">];
  let SemaHandler = 0;
  let Documentation = [InitSegDocs];
  let AdditionalMembers = [{
  void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const {
    OS << " (" << getSection() << ')';
  }
  }];
}

def LoopHint : Attr {
  /// #pragma clang loop <option> directive
  /// vectorize: vectorizes loop operations if State == Enable.
  /// vectorize_width: vectorize loop operations with width 'Value'.
  /// interleave: interleave multiple loop iterations if State == Enable.
  /// interleave_count: interleaves 'Value' loop iterations.
  /// unroll: fully unroll loop if State == Enable.
  /// unroll_count: unrolls loop 'Value' times.
  /// unroll_and_jam: attempt to unroll and jam loop if State == Enable.
  /// unroll_and_jam_count: unroll and jams loop 'Value' times.
  /// distribute: attempt to distribute loop if State == Enable.
  /// pipeline: disable pipelining loop if State == Disable.
  /// pipeline_initiation_interval: create loop schedule with initiation interval equal to 'Value'.

  /// #pragma unroll <argument> directive
  /// <no arg>: fully unrolls loop.
  /// boolean: fully unrolls loop if State == Enable.
  /// expression: unrolls loop 'Value' times.

  let Spellings = [Pragma<"clang", "loop">, Pragma<"", "unroll">,
                   Pragma<"", "nounroll">, Pragma<"", "unroll_and_jam">,
                   Pragma<"", "nounroll_and_jam">];

  /// State of the loop optimization specified by the spelling.
  let Args = [EnumArgument<"Option", "OptionType",
                          ["vectorize", "vectorize_width", "interleave", "interleave_count",
                           "unroll", "unroll_count", "unroll_and_jam", "unroll_and_jam_count",
                           "pipeline", "pipeline_initiation_interval", "distribute"],
                          ["Vectorize", "VectorizeWidth", "Interleave", "InterleaveCount",
                           "Unroll", "UnrollCount", "UnrollAndJam", "UnrollAndJamCount",
                           "PipelineDisabled", "PipelineInitiationInterval", "Distribute"]>,
              EnumArgument<"State", "LoopHintState",
                           ["enable", "disable", "numeric", "assume_safety", "full"],
                           ["Enable", "Disable", "Numeric", "AssumeSafety", "Full"]>,
              ExprArgument<"Value">];

  let AdditionalMembers = [{
  static const char *getOptionName(int Option) {
    switch(Option) {
    case Vectorize: return "vectorize";
    case VectorizeWidth: return "vectorize_width";
    case Interleave: return "interleave";
    case InterleaveCount: return "interleave_count";
    case Unroll: return "unroll";
    case UnrollCount: return "unroll_count";
    case UnrollAndJam: return "unroll_and_jam";
    case UnrollAndJamCount: return "unroll_and_jam_count";
    case PipelineDisabled: return "pipeline";
    case PipelineInitiationInterval: return "pipeline_initiation_interval";
    case Distribute: return "distribute";
    }
    llvm_unreachable("Unhandled LoopHint option.");
  }

  void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const {
    unsigned SpellingIndex = getSpellingListIndex();
    // For "#pragma unroll" and "#pragma nounroll" the string "unroll" or
    // "nounroll" is already emitted as the pragma name.
    if (SpellingIndex == Pragma_nounroll || SpellingIndex == Pragma_nounroll_and_jam)
      return;
    else if (SpellingIndex == Pragma_unroll || SpellingIndex == Pragma_unroll_and_jam) {
      OS << ' ' << getValueString(Policy);
      return;
    }

    assert(SpellingIndex == Pragma_clang_loop && "Unexpected spelling");
    OS << ' ' << getOptionName(option) << getValueString(Policy);
  }

  // Return a string containing the loop hint argument including the
  // enclosing parentheses.
  std::string getValueString(const PrintingPolicy &Policy) const {
    std::string ValueName;
    llvm::raw_string_ostream OS(ValueName);
    OS << "(";
    if (state == Numeric)
      value->printPretty(OS, nullptr, Policy);
    else if (state == Enable)
      OS << "enable";
    else if (state == Full)
      OS << "full";
    else if (state == AssumeSafety)
      OS << "assume_safety";
    else
      OS << "disable";
    OS << ")";
    return OS.str();
  }

  // Return a string suitable for identifying this attribute in diagnostics.
  std::string getDiagnosticName(const PrintingPolicy &Policy) const {
    unsigned SpellingIndex = getSpellingListIndex();
    if (SpellingIndex == Pragma_nounroll)
      return "#pragma nounroll";
    else if (SpellingIndex == Pragma_unroll)
      return "#pragma unroll" + (option == UnrollCount ? getValueString(Policy) : "");
    else if (SpellingIndex == Pragma_nounroll_and_jam)
      return "#pragma nounroll_and_jam";
    else if (SpellingIndex == Pragma_unroll_and_jam)
      return "#pragma unroll_and_jam" +
        (option == UnrollAndJamCount ? getValueString(Policy) : "");

    assert(SpellingIndex == Pragma_clang_loop && "Unexpected spelling");
    return getOptionName(option) + getValueString(Policy);
  }
  }];

  let Documentation = [LoopHintDocs, UnrollHintDocs];
}

def CapturedRecord : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def OMPThreadPrivateDecl : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def OMPCaptureNoInit : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [Undocumented];
}

def OMPCaptureKind : Attr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Args = [UnsignedArgument<"CaptureKind">];
  let Documentation = [Undocumented];
}

def OMPReferencedVar : Attr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Args = [ExprArgument<"Ref">];
  let Documentation = [Undocumented];
}

def OMPDeclareSimdDecl : Attr {
  let Spellings = [Pragma<"omp", "declare simd">];
  let Subjects = SubjectList<[Function]>;
  let SemaHandler = 0;
  let HasCustomParsing = 1;
  let Documentation = [OMPDeclareSimdDocs];
  let Args = [
    EnumArgument<"BranchState", "BranchStateTy",
                 [ "", "inbranch", "notinbranch" ],
                 [ "BS_Undefined", "BS_Inbranch", "BS_Notinbranch" ]>,
    ExprArgument<"Simdlen">, VariadicExprArgument<"Uniforms">,
    VariadicExprArgument<"Aligneds">, VariadicExprArgument<"Alignments">,
    VariadicExprArgument<"Linears">, VariadicUnsignedArgument<"Modifiers">,
    VariadicExprArgument<"Steps">
  ];
  let AdditionalMembers = [{
    void printPrettyPragma(raw_ostream & OS, const PrintingPolicy &Policy)
        const {
      if (getBranchState() != BS_Undefined)
        OS << ' ' << ConvertBranchStateTyToStr(getBranchState());
      if (auto *E = getSimdlen()) {
        OS << " simdlen(";
        E->printPretty(OS, nullptr, Policy);
        OS << ")";
      }
      if (uniforms_size() > 0) {
        OS << " uniform";
        StringRef Sep = "(";
        for (auto *E : uniforms()) {
          OS << Sep;
          E->printPretty(OS, nullptr, Policy);
          Sep = ", ";
        }
        OS << ")";
      }
      alignments_iterator NI = alignments_begin();
      for (auto *E : aligneds()) {
        OS << " aligned(";
        E->printPretty(OS, nullptr, Policy);
        if (*NI) {
          OS << ": ";
          (*NI)->printPretty(OS, nullptr, Policy);
        }
        OS << ")";
        ++NI;
      }
      steps_iterator I = steps_begin();
      modifiers_iterator MI = modifiers_begin();
      for (auto *E : linears()) {
        OS << " linear(";
        if (*MI != OMPC_LINEAR_unknown)
          OS << getOpenMPSimpleClauseTypeName(OMPC_linear, *MI) << "(";
        E->printPretty(OS, nullptr, Policy);
        if (*MI != OMPC_LINEAR_unknown)
          OS << ")";
        if (*I) {
          OS << ": ";
          (*I)->printPretty(OS, nullptr, Policy);
        }
        OS << ")";
        ++I;
        ++MI;
      }
    }
  }];
}

def OMPDeclareTargetDecl : InheritableAttr {
  let Spellings = [Pragma<"omp", "declare target">];
  let SemaHandler = 0;
  let Subjects = SubjectList<[Function, SharedVar]>;
  let Documentation = [OMPDeclareTargetDocs];
  let Args = [
    EnumArgument<"MapType", "MapTypeTy",
                 [ "to", "link" ],
                 [ "MT_To", "MT_Link" ]>
  ];
  let AdditionalMembers = [{
    void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const {
      // Use fake syntax because it is for testing and debugging purpose only.
      if (getMapType() != MT_To)
        OS << ' ' << ConvertMapTypeTyToStr(getMapType());
    }
    static llvm::Optional<MapTypeTy>
    isDeclareTargetDeclaration(const ValueDecl *VD) {
      if (!VD->hasAttrs())
        return llvm::None;
      if (const auto *Attr = VD->getAttr<OMPDeclareTargetDeclAttr>())
        return Attr->getMapType();

      return llvm::None;
    }
  }];
}

def InternalLinkage : InheritableAttr {
  let Spellings = [Clang<"internal_linkage">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [InternalLinkageDocs];
}

def ExcludeFromExplicitInstantiation : InheritableAttr {
  let Spellings = [Clang<"exclude_from_explicit_instantiation">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [ExcludeFromExplicitInstantiationDocs];
  let MeaningfulToClassTemplateDefinition = 1;
}

def Reinitializes : InheritableAttr {
  let Spellings = [Clang<"reinitializes", 0>];
  let Subjects = SubjectList<[NonStaticNonConstCXXMethod], ErrorDiag>;
  let Documentation = [ReinitializesDocs];
}

def NoDestroy : InheritableAttr {
  let Spellings = [Clang<"no_destroy", 0>];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [NoDestroyDocs];
}

def AlwaysDestroy : InheritableAttr {
  let Spellings = [Clang<"always_destroy", 0>];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [AlwaysDestroyDocs];
}

def SpeculativeLoadHardening : InheritableAttr {
  let Spellings = [Clang<"speculative_load_hardening">];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [SpeculativeLoadHardeningDocs];
}

def Uninitialized : InheritableAttr {
  let Spellings = [Clang<"uninitialized", 0>];
  let Subjects = SubjectList<[LocalVar]>;
  let Documentation = [UninitializedDocs];
}

def ObjCExternallyRetained : InheritableAttr {
  let LangOpts = [ObjCAutoRefCount];
  let Spellings = [Clang<"objc_externally_retained">];
  let Subjects = SubjectList<[NonParmVar, Function, Block, ObjCMethod]>;
  let Documentation = [ObjCExternallyRetainedDocs];
}
