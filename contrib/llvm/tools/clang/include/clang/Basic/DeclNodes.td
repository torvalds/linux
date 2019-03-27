class AttrSubject;

class Decl<string diagSpelling = "", bit abstract = 0> : AttrSubject {
  bit Abstract = abstract;
  string DiagSpelling = diagSpelling;
}

class DDecl<Decl base, string diagSpelling = "", bit abstract = 0>
    : Decl<diagSpelling, abstract> {
  Decl Base = base;
}

class DeclContext {}

def TranslationUnit : Decl, DeclContext;
def PragmaComment : Decl;
def PragmaDetectMismatch : Decl;
def ExternCContext : Decl, DeclContext;
def Named : Decl<"named declarations", 1>;
  def Namespace : DDecl<Named, "namespaces">, DeclContext;
  def UsingDirective : DDecl<Named>;
  def NamespaceAlias : DDecl<Named>;
  def Label : DDecl<Named, "labels">;
  def Type : DDecl<Named, "types", 1>;
    def TypedefName : DDecl<Type, "typedefs", 1>;
      def Typedef : DDecl<TypedefName>;
      def TypeAlias : DDecl<TypedefName>;
      def ObjCTypeParam : DDecl<TypedefName>;
    def UnresolvedUsingTypename : DDecl<Type>;
    def Tag : DDecl<Type, "tag types", 1>, DeclContext;
      def Enum : DDecl<Tag, "enums">;
      def Record : DDecl<Tag, "structs, unions, classes">;
        def CXXRecord : DDecl<Record, "classes">;
          def ClassTemplateSpecialization : DDecl<CXXRecord>;
            def ClassTemplatePartialSpecialization
              : DDecl<ClassTemplateSpecialization>;
    def TemplateTypeParm : DDecl<Type>;
  def Value : DDecl<Named, "value declarations", 1>;
    def EnumConstant : DDecl<Value, "enumerators">;
    def UnresolvedUsingValue : DDecl<Value>;
    def IndirectField : DDecl<Value>;
    def Binding : DDecl<Value>;
    def OMPDeclareReduction : DDecl<Value>, DeclContext;
    def Declarator : DDecl<Value, "declarators", 1>;
      def Field : DDecl<Declarator, "non-static data members">;
        def ObjCIvar : DDecl<Field>;
        def ObjCAtDefsField : DDecl<Field>;
      def MSProperty : DDecl<Declarator>;
      def Function : DDecl<Declarator, "functions">, DeclContext;
        def CXXDeductionGuide : DDecl<Function>;
        def CXXMethod : DDecl<Function>;
          def CXXConstructor : DDecl<CXXMethod>;
          def CXXDestructor : DDecl<CXXMethod>;
          def CXXConversion : DDecl<CXXMethod>;
      def Var : DDecl<Declarator, "variables">;
        def VarTemplateSpecialization : DDecl<Var>;
          def VarTemplatePartialSpecialization
            : DDecl<VarTemplateSpecialization>;
        def ImplicitParam : DDecl<Var>;
        def ParmVar : DDecl<Var, "parameters">;
        def Decomposition : DDecl<Var>;
        def OMPCapturedExpr : DDecl<Var>;
      def NonTypeTemplateParm : DDecl<Declarator>;
  def Template : DDecl<Named, "templates", 1>;
    def RedeclarableTemplate : DDecl<Template, "redeclarable templates", 1>;
      def FunctionTemplate : DDecl<RedeclarableTemplate>;
      def ClassTemplate : DDecl<RedeclarableTemplate>;
      def VarTemplate : DDecl<RedeclarableTemplate>;
      def TypeAliasTemplate : DDecl<RedeclarableTemplate>;
    def TemplateTemplateParm : DDecl<Template>;
    def BuiltinTemplate : DDecl<Template>;
  def Using : DDecl<Named>;
  def UsingPack : DDecl<Named>;
  def UsingShadow : DDecl<Named>;
    def ConstructorUsingShadow : DDecl<UsingShadow>;
  def ObjCMethod : DDecl<Named, "Objective-C methods">, DeclContext;
  def ObjCContainer : DDecl<Named, "Objective-C containers", 1>, DeclContext;
    def ObjCCategory : DDecl<ObjCContainer>;
    def ObjCProtocol : DDecl<ObjCContainer, "Objective-C protocols">;
    def ObjCInterface : DDecl<ObjCContainer, "Objective-C interfaces">;
    def ObjCImpl
        : DDecl<ObjCContainer, "Objective-C implementation declarations", 1>;
      def ObjCCategoryImpl : DDecl<ObjCImpl>;
      def ObjCImplementation : DDecl<ObjCImpl>;
  def ObjCProperty : DDecl<Named, "Objective-C properties">;
  def ObjCCompatibleAlias : DDecl<Named>;
def LinkageSpec : Decl, DeclContext;
def Export : Decl, DeclContext;
def ObjCPropertyImpl : Decl;
def FileScopeAsm : Decl;
def AccessSpec : Decl;
def Friend : Decl;
def FriendTemplate : Decl;
def StaticAssert : Decl;
def Block : Decl<"blocks">, DeclContext;
def Captured : Decl, DeclContext;
def ClassScopeFunctionSpecialization : Decl;
def Import : Decl;
def OMPThreadPrivate : Decl;
def OMPRequires : Decl;
def Empty : Decl;

