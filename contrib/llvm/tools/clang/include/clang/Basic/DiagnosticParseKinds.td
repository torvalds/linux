//==--- DiagnosticParseKinds.td - libparse diagnostics --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Parser Diagnostics
//===----------------------------------------------------------------------===//

let Component = "Parse" in {

def warn_asm_qualifier_ignored : Warning<
  "ignored %0 qualifier on asm">, CatInlineAsm, InGroup<ASMIgnoredQualifier>;
def warn_file_asm_volatile : Warning<
  "meaningless 'volatile' on asm outside function">, CatInlineAsm,
  InGroup<ASMIgnoredQualifier>;

let CategoryName = "Inline Assembly Issue" in {
def err_asm_empty : Error<"__asm used with no assembly instructions">;
def err_inline_ms_asm_parsing : Error<"%0">;
def err_msasm_unsupported_arch : Error<
  "Unsupported architecture '%0' for MS-style inline assembly">;
def err_msasm_unable_to_create_target : Error<
  "MS-style inline assembly is not available: %0">;
def err_gnu_inline_asm_disabled : Error<
  "GNU-style inline assembly is disabled">;
def err_asm_goto_not_supported_yet : Error<
  "'asm goto' constructs are not supported yet">;
}

let CategoryName = "Parse Issue" in {

def ext_empty_translation_unit : Extension<
  "ISO C requires a translation unit to contain at least one declaration">,
  InGroup<DiagGroup<"empty-translation-unit">>;
def warn_cxx98_compat_top_level_semi : Warning<
  "extra ';' outside of a function is incompatible with C++98">,
  InGroup<CXX98CompatExtraSemi>, DefaultIgnore;
def ext_extra_semi : Extension<
  "extra ';' %select{"
  "outside of a function|"
  "inside a %1|"
  "inside instance variable list|"
  "after member function definition}0">,
  InGroup<ExtraSemi>;
def ext_extra_semi_cxx11 : Extension<
  "extra ';' outside of a function is a C++11 extension">,
  InGroup<CXX11ExtraSemi>;
def warn_extra_semi_after_mem_fn_def : Warning<
  "extra ';' after member function definition">,
  InGroup<ExtraSemi>, DefaultIgnore;
def warn_null_statement : Warning<
  "empty expression statement has no effect; "
  "remove unnecessary ';' to silence this warning">,
  InGroup<ExtraSemiStmt>, DefaultIgnore;

def ext_thread_before : Extension<"'__thread' before '%0'">;
def ext_keyword_as_ident : ExtWarn<
  "keyword '%0' will be made available as an identifier "
  "%select{here|for the remainder of the translation unit}1">,
  InGroup<KeywordCompat>;

def ext_nullability : Extension<
  "type nullability specifier %0 is a Clang extension">,
  InGroup<DiagGroup<"nullability-extension">>;

def err_empty_enum : Error<"use of empty enum">;

def ext_ident_list_in_param : Extension<
  "type-less parameter names in function declaration">;
def ext_c99_variable_decl_in_for_loop : Extension<
  "variable declaration in for loop is a C99-specific feature">, InGroup<C99>;
def ext_c99_compound_literal : Extension<
  "compound literals are a C99-specific feature">, InGroup<C99>;
def ext_enumerator_list_comma_c : Extension<
  "commas at the end of enumerator lists are a C99-specific "
  "feature">, InGroup<C99>;
def ext_enumerator_list_comma_cxx : Extension<
  "commas at the end of enumerator lists are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_enumerator_list_comma : Warning<
  "commas at the end of enumerator lists are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def err_enumerator_list_missing_comma : Error<
  "missing ',' between enumerators">;
def err_enumerator_unnamed_no_def : Error<
  "unnamed enumeration must be a definition">;
def ext_cxx11_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a C++11 extension">,
  InGroup<CXX11>;
def ext_ms_c_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a Microsoft extension">,
  InGroup<MicrosoftFixedEnum>;
def ext_clang_c_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a Clang extension">,
  InGroup<DiagGroup<"fixed-enum-extension">>;
def warn_cxx98_compat_enum_fixed_underlying_type : Warning<
  "enumeration types with a fixed underlying type are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_alignof : Warning<
  "alignof expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_alignof_expr : ExtWarn<
  "%0 applied to an expression is a GNU extension">, InGroup<GNUAlignofExpression>;

def warn_microsoft_dependent_exists : Warning<
  "dependent %select{__if_not_exists|__if_exists}0 declarations are ignored">,
  InGroup<DiagGroup<"microsoft-exists">>;
def warn_microsoft_qualifiers_ignored : Warning<
  "qualifiers after comma in declarator list are ignored">,
  InGroup<IgnoredAttributes>;

def ext_c11_generic_selection : Extension<
  "generic selections are a C11-specific feature">, InGroup<C11>;
def err_duplicate_default_assoc : Error<
  "duplicate default generic association">;
def note_previous_default_assoc : Note<
  "previous default generic association is here">;

def ext_c11_alignment : Extension<
  "%0 is a C11-specific feature">, InGroup<C11>;

def ext_c11_noreturn : Extension<
  "_Noreturn functions are a C11-specific feature">, InGroup<C11>;
def err_c11_noreturn_misplaced : Error<
  "'_Noreturn' keyword must precede function declarator">;

def ext_gnu_indirect_goto : Extension<
  "use of GNU indirect-goto extension">, InGroup<GNULabelsAsValue>;
def ext_gnu_address_of_label : Extension<
  "use of GNU address-of-label extension">, InGroup<GNULabelsAsValue>;
def err_stmtexpr_file_scope : Error<
  "statement expression not allowed at file scope">;
def ext_gnu_statement_expr : Extension<
  "use of GNU statement expression extension">, InGroup<GNUStatementExpression>;
def ext_gnu_conditional_expr : Extension<
  "use of GNU ?: conditional expression extension, omitting middle operand">, InGroup<GNUConditionalOmittedOperand>;
def ext_gnu_empty_initializer : Extension<
  "use of GNU empty initializer extension">, InGroup<GNUEmptyInitializer>;
def ext_gnu_array_range : Extension<"use of GNU array range extension">,
  InGroup<GNUDesignator>;
def ext_gnu_missing_equal_designator : ExtWarn<
  "use of GNU 'missing =' extension in designator">,
  InGroup<GNUDesignator>;
def err_expected_equal_designator : Error<"expected '=' or another designator">;
def ext_gnu_old_style_field_designator : ExtWarn<
  "use of GNU old-style field designator extension">,
  InGroup<GNUDesignator>;
def ext_gnu_case_range : Extension<"use of GNU case range extension">,
  InGroup<GNUCaseRange>;

// Generic errors.
def err_expected_expression : Error<"expected expression">;
def err_expected_type : Error<"expected a type">;
def err_expected_external_declaration : Error<"expected external declaration">;
def err_extraneous_closing_brace : Error<"extraneous closing brace ('}')">;
def err_expected_semi_declaration : Error<
  "expected ';' at end of declaration">;
def err_expected_semi_decl_list : Error<
  "expected ';' at end of declaration list">;
def ext_expected_semi_decl_list : ExtWarn<
  "expected ';' at end of declaration list">;
def err_expected_member_name_or_semi : Error<
  "expected member name or ';' after declaration specifiers">;
def err_function_declared_typedef : Error<
  "function definition declared 'typedef'">;
def err_at_defs_cxx : Error<"@defs is not supported in Objective-C++">;
def err_at_in_class : Error<"unexpected '@' in member specification">;
def err_unexpected_semi : Error<"unexpected ';' before %0">;

def err_expected_fn_body : Error<
  "expected function body after function declarator">;
def warn_attribute_on_function_definition : Warning<
  "GCC does not allow %0 attribute in this position on a function definition">,
  InGroup<GccCompat>;
def warn_gcc_attribute_location : Warning<
  "GCC does not allow an attribute in this position on a function declaration">,
  InGroup<GccCompat>;
def warn_gcc_variable_decl_in_for_loop : Warning<
  "GCC does not allow variable declarations in for loop initializers before "
  "C99">, InGroup<GccCompat>;
def warn_attribute_no_decl : Warning<
  "attribute %0 ignored, because it is not attached to a declaration">,
  InGroup<IgnoredAttributes>;
def err_ms_attributes_not_enabled : Error<
  "'__declspec' attributes are not enabled; use '-fdeclspec' or "
  "'-fms-extensions' to enable support for __declspec attributes">;
def err_expected_method_body : Error<"expected method body">;
def err_declspec_after_virtspec : Error<
  "'%0' qualifier may not appear after the virtual specifier '%1'">;
def err_invalid_token_after_toplevel_declarator : Error<
  "expected ';' after top level declarator">;
def err_invalid_token_after_declarator_suggest_equal : Error<
  "invalid %0 at end of declaration; did you mean '='?">;
def err_expected_statement : Error<"expected statement">;
def err_expected_lparen_after : Error<"expected '(' after '%0'">;
def err_expected_rparen_after : Error<"expected ')' after '%0'">;
def err_expected_punc : Error<"expected ')' or ',' after '%0'">;
def err_expected_less_after : Error<"expected '<' after '%0'">;
def err_expected_lbrace_in_compound_literal : Error<
  "expected '{' in compound literal">;
def err_expected_while : Error<"expected 'while' in do/while loop">;

def err_expected_semi_after_stmt : Error<"expected ';' after %0 statement">;
def err_expected_semi_after_expr : Error<"expected ';' after expression">;
def err_extraneous_token_before_semi : Error<"extraneous '%0' before ';'">;

def err_expected_semi_after_method_proto : Error<
  "expected ';' after method prototype">;
def err_expected_semi_after_namespace_name : Error<
  "expected ';' after namespace name">;
def err_unexpected_namespace_attributes_alias : Error<
  "attributes cannot be specified on namespace alias">;
def err_unexpected_nested_namespace_attribute : Error<
  "attributes cannot be specified on a nested namespace definition">;
def err_inline_namespace_alias : Error<"namespace alias cannot be inline">;
def err_namespace_nonnamespace_scope : Error<
  "namespaces can only be defined in global or namespace scope">;
def ext_nested_namespace_definition : ExtWarn<
  "nested namespace definition is a C++17 extension; "
  "define each namespace separately">, InGroup<CXX17>;
def warn_cxx14_compat_nested_namespace_definition : Warning<
  "nested namespace definition is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_inline_nested_namespace_definition : ExtWarn<
  "inline nested namespace definition is a C++2a extension">, InGroup<CXX2a>;
def warn_cxx17_compat_inline_nested_namespace_definition : Warning<
  "inline nested namespace definition is incompatible with C++ standards before"
  " C++2a">, InGroup<CXXPre2aCompat>, DefaultIgnore;
def err_inline_nested_namespace_definition : Error<
  "nested namespace definition cannot be 'inline'">;
def err_expected_semi_after_attribute_list : Error<
  "expected ';' after attribute list">;
def err_expected_semi_after_static_assert : Error<
  "expected ';' after static_assert">;
def err_expected_semi_for : Error<"expected ';' in 'for' statement specifier">;
def err_single_decl_assign_in_for_range : Error<
  "range-based 'for' statement uses ':', not '='">;
def warn_missing_selector_name : Warning<
  "%0 used as the name of the previous parameter rather than as part "
  "of the selector">,
  InGroup<DiagGroup<"missing-selector-name">>;
def note_missing_selector_name : Note<
  "introduce a parameter name to make %0 part of the selector">;
def note_force_empty_selector_name : Note<
  "or insert whitespace before ':' to use %0 as parameter name "
  "and have an empty entry in the selector">;
def err_label_end_of_compound_statement : Error<
  "label at end of compound statement: expected statement">;
def err_address_of_label_outside_fn : Error<
  "use of address-of-label extension outside of a function body">;
def err_asm_operand_wide_string_literal : Error<
  "cannot use %select{unicode|wide}0 string literal in 'asm'">;
def err_expected_selector_for_method : Error<
  "expected selector for Objective-C method">;
def err_expected_property_name : Error<"expected property name">;

def err_unexpected_at : Error<"unexpected '@' in program">;
def err_atimport : Error<
"use of '@import' when modules are disabled">;

def warn_atimport_in_framework_header : Warning<
  "use of '@import' in framework header is discouraged, "
  "including this header requires -fmodules">,
  InGroup<FrameworkHdrAtImport>;

def err_invalid_reference_qualifier_application : Error<
  "'%0' qualifier may not be applied to a reference">;
def err_illegal_decl_reference_to_reference : Error<
  "%0 declared as a reference to a reference">;
def ext_rvalue_reference : ExtWarn<
  "rvalue references are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_rvalue_reference : Warning<
  "rvalue references are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_ref_qualifier : ExtWarn<
  "reference qualifiers on functions are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_ref_qualifier : Warning<
  "reference qualifiers on functions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_inline_namespace : ExtWarn<
  "inline namespaces are a C++11 feature">, InGroup<CXX11InlineNamespace>;
def warn_cxx98_compat_inline_namespace : Warning<
  "inline namespaces are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_generalized_initializer_lists : ExtWarn<
  "generalized initializer lists are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_generalized_initializer_lists : Warning<
  "generalized initializer lists are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_init_list_bin_op : Error<"initializer list cannot be used on the "
  "%select{left|right}0 hand side of operator '%1'">;
def warn_cxx98_compat_trailing_return_type : Warning<
  "trailing return types are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_auto_storage_class : ExtWarn<
  "'auto' storage class specifier is not permitted in C++11, and will not "
  "be supported in future releases">, InGroup<DiagGroup<"auto-storage-class">>;
def ext_decltype_auto_type_specifier : ExtWarn<
  "'decltype(auto)' type specifier is a C++14 extension">, InGroup<CXX14>;
def warn_cxx11_compat_decltype_auto_type_specifier : Warning<
  "'decltype(auto)' type specifier is incompatible with C++ standards before "
  "C++14">, InGroup<CXXPre14Compat>, DefaultIgnore;
def ext_auto_type : Extension<
  "'__auto_type' is a GNU extension">,
  InGroup<GNUAutoType>;
def ext_for_range : ExtWarn<
  "range-based for loop is a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_for_range : Warning<
  "range-based for loop is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_for_range_identifier : Error<
  "range-based for loop requires type for loop variable">;
def err_for_range_expected_decl : Error<
  "for range declaration must declare a variable">;
def err_argument_required_after_attribute : Error<
  "argument required after attribute">;
def err_missing_param : Error<"expected parameter declarator">;
def err_missing_comma_before_ellipsis : Error<
  "C requires a comma prior to the ellipsis in a variadic function type">;
def err_unexpected_typedef_ident : Error<
  "unexpected type name %0: expected identifier">;
def warn_cxx98_compat_decltype : Warning<
  "'decltype' type specifier is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_unexpected_scope_on_base_decltype : Error<
  "unexpected namespace scope prior to decltype">;
def err_expected_class_name : Error<"expected class name">;
def err_expected_class_name_not_template :
  Error<"'typename' is redundant; base classes are implicitly types">;
def err_unspecified_vla_size_with_static : Error<
  "'static' may not be used with an unspecified variable length array size">;
def err_unspecified_size_with_static : Error<
  "'static' may not be used without an array size">;
def err_expected_parentheses_around_typename : Error<
  "expected parentheses around type name in %0 expression">;

def err_expected_case_before_expression: Error<
  "expected 'case' keyword before expression">;

def ext_warn_gnu_final : ExtWarn<
  "__final is a GNU extension, consider using C++11 final">,
  InGroup<GccCompat>;

// Declarations.
def err_typename_requires_specqual : Error<
  "type name requires a specifier or qualifier">;
def err_typename_invalid_storageclass : Error<
  "type name does not allow storage class to be specified">;
def err_typename_invalid_functionspec : Error<
  "type name does not allow function specifier to be specified">;
def err_typename_invalid_constexpr : Error<
  "type name does not allow constexpr specifier to be specified">;
def err_typename_identifiers_only : Error<
  "typename is allowed for identifiers only">;

def err_friend_invalid_in_context : Error<
  "'friend' used outside of class">;
def err_templated_using_directive_declaration : Error<
  "cannot template a using %select{directive|declaration}0">;
def err_unexpected_colon_in_nested_name_spec : Error<
  "unexpected ':' in nested name specifier; did you mean '::'?">;
def err_unexpected_token_in_nested_name_spec : Error<
  "'%0' cannot be a part of nested name specifier; did you mean ':'?">;
def err_bool_redeclaration : Error<
  "redeclaration of C++ built-in type 'bool'">;
def ext_c11_static_assert : Extension<
  "_Static_assert is a C11-specific feature">, InGroup<C11>;
def warn_cxx98_compat_static_assert : Warning<
  "static_assert declarations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_static_assert_no_message : ExtWarn<
  "static_assert with no message is a C++17 extension">, InGroup<CXX17>;
def warn_cxx14_compat_static_assert_no_message : Warning<
  "static_assert with no message is incompatible with C++ standards before "
  "C++17">,
  DefaultIgnore, InGroup<CXXPre17Compat>;
def err_function_definition_not_allowed : Error<
  "function definition is not allowed here">;
def err_expected_end_of_enumerator : Error<
  "expected '= constant-expression' or end of enumerator definition">;
def err_expected_coloncolon_after_super : Error<
  "expected '::' after '__super'">;

def ext_decomp_decl_empty : ExtWarn<
  "ISO C++17 does not allow a decomposition group to be empty">,
  InGroup<DiagGroup<"empty-decomposition">>;

/// Objective-C parser diagnostics
def err_expected_minus_or_plus : Error<
  "method type specifier must start with '-' or '+'">;
def err_objc_missing_end : Error<"missing '@end'">;
def note_objc_container_start : Note<
  "%select{class|protocol|category|class extension|implementation"
  "|category implementation}0 started here">;
def warn_objc_protocol_qualifier_missing_id : Warning<
  "protocol has no object type specified; defaults to qualified 'id'">;
def err_objc_unknown_at : Error<"expected an Objective-C directive after '@'">;
def err_illegal_super_cast : Error<
  "cannot cast 'super' (it isn't an expression)">;
def err_nsnumber_nonliteral_unary : Error<
  "@%0 must be followed by a number to form an NSNumber object">;
def warn_cstyle_param : Warning<
  "use of C-style parameters in Objective-C method declarations"
  " is deprecated">, InGroup<DeprecatedDeclarations>;

let CategoryName = "ARC Parse Issue" in {
def err_arc_bridge_retain : Error<
  "unknown cast annotation __bridge_retain; did you mean __bridge_retained?">;
// To be default mapped to an error later.
def warn_arc_bridge_cast_nonarc : Warning<
  "'%0' casts have no effect when not using ARC">,
  InGroup<DiagGroup<"arc-bridge-casts-disallowed-in-nonarc">>;
}

def err_objc_illegal_visibility_spec : Error<
  "illegal visibility specification">;
def err_objc_illegal_interface_qual : Error<"illegal interface qualifier">;
def err_objc_expected_equal_for_getter : Error<
  "expected '=' for Objective-C getter">;
def err_objc_expected_equal_for_setter : Error<
  "expected '=' for Objective-C setter">;
def err_objc_expected_selector_for_getter_setter : Error<
  "expected selector for Objective-C %select{setter|getter}0">;
def err_objc_property_requires_field_name : Error<
  "property requires fields to be named">;
def err_objc_property_bitfield : Error<"property name cannot be a bit-field">;
def err_objc_expected_property_attr : Error<"unknown property attribute %0">;
def err_objc_properties_require_objc2 : Error<
  "properties are an Objective-C 2 feature">;
def err_objc_unexpected_attr : Error<
  "prefix attribute must be followed by an interface or protocol">;
def err_objc_postfix_attribute : Error <
  "postfix attributes are not allowed on Objective-C directives">;
def err_objc_postfix_attribute_hint : Error <
  "postfix attributes are not allowed on Objective-C directives, place"
  " them in front of '%select{@interface|@protocol}0'">;
def err_objc_directive_only_in_protocol : Error<
  "directive may only be specified in protocols only">;
def err_missing_catch_finally : Error<
  "@try statement without a @catch and @finally clause">;
def err_objc_concat_string : Error<"unexpected token after Objective-C string">;
def err_expected_objc_container : Error<
  "'@end' must appear in an Objective-C context">;
def err_unexpected_protocol_qualifier : Error<
  "@implementation declaration cannot be protocol qualified">;
def err_objc_unexpected_atend : Error<
  "'@end' appears where closing brace '}' is expected">;
def err_synthesized_property_name : Error<
  "expected a property name in @synthesize">;
def warn_semicolon_before_method_body : Warning<
  "semicolon before method body is ignored">,
  InGroup<SemiBeforeMethodBody>, DefaultIgnore;
def note_extra_comma_message_arg : Note<
  "comma separating Objective-C messaging arguments">;

def err_expected_field_designator : Error<
  "expected a field designator, such as '.field = 4'">;

def err_declaration_does_not_declare_param : Error<
  "declaration does not declare a parameter">;
def err_no_matching_param : Error<"parameter named %0 is missing">;

/// Objective-C++ parser diagnostics
def err_expected_token_instead_of_objcxx_keyword : Error<
  "expected %0; %1 is a keyword in Objective-C++">;
def err_expected_member_name_or_semi_objcxx_keyword : Error<
  "expected member name or ';' after declaration specifiers; "
  "%0 is a keyword in Objective-C++">;

/// C++ parser diagnostics
def err_invalid_operator_on_type : Error<
  "cannot use %select{dot|arrow}0 operator on a type">;
def err_expected_unqualified_id : Error<
  "expected %select{identifier|unqualified-id}0">;
def err_brackets_go_after_unqualified_id : Error<
  "brackets are not allowed here; to declare an array, "
  "place the brackets after the %select{identifier|name}0">;
def err_unexpected_unqualified_id : Error<"type-id cannot have a name">;
def err_func_def_no_params : Error<
  "function definition does not declare parameters">;
def err_expected_lparen_after_type : Error<
  "expected '(' for function-style cast or type construction">;
def err_expected_init_in_condition : Error<
  "variable declaration in condition must have an initializer">;
def err_expected_init_in_condition_lparen : Error<
  "variable declaration in condition cannot have a parenthesized initializer">;
def err_extraneous_rparen_in_condition : Error<
  "extraneous ')' after condition, expected a statement">;
def warn_dangling_else : Warning<
  "add explicit braces to avoid dangling else">,
  InGroup<DanglingElse>;
def err_expected_member_or_base_name : Error<
  "expected class member or base class name">;
def err_expected_lbrace_after_base_specifiers : Error<
  "expected '{' after base class list">;
def err_missing_end_of_definition : Error<
  "missing '}' at end of definition of %q0">;
def note_missing_end_of_definition_before : Note<
  "still within definition of %q0 here">;
def ext_ellipsis_exception_spec : Extension<
  "exception specification of '...' is a Microsoft extension">,
  InGroup<MicrosoftExceptionSpec>;
def err_dynamic_and_noexcept_specification : Error<
  "cannot have both throw() and noexcept() clause on the same function">;
def err_except_spec_unparsed : Error<
  "unexpected end of exception specification">;
def ext_dynamic_exception_spec : ExtWarn<
  "ISO C++17 does not allow dynamic exception specifications">,
  InGroup<DynamicExceptionSpec>, DefaultError;
def warn_exception_spec_deprecated : Warning<
  "dynamic exception specifications are deprecated">,
  InGroup<DeprecatedDynamicExceptionSpec>, DefaultIgnore;
def note_exception_spec_deprecated : Note<"use '%0' instead">;
def warn_cxx98_compat_noexcept_decl : Warning<
  "noexcept specifications are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_expected_catch : Error<"expected catch">;
def err_using_namespace_in_class : Error<
  "'using namespace' is not allowed in classes">;
def err_constructor_bad_name : Error<
  "missing return type for function %0; did you mean the constructor name %1?">;
def err_destructor_tilde_identifier : Error<
  "expected a class name after '~' to name a destructor">;
def err_destructor_tilde_scope : Error<
  "'~' in destructor name should be after nested name specifier">;
def err_destructor_template_id : Error<
  "destructor name %0 does not refer to a template">;
def err_default_arg_unparsed : Error<
  "unexpected end of default argument expression">;
def err_bracket_depth_exceeded : Error<
  "bracket nesting level exceeded maximum of %0">, DefaultFatal;
def note_bracket_depth : Note<
  "use -fbracket-depth=N to increase maximum nesting level">;
def err_misplaced_ellipsis_in_declaration : Error<
  "'...' must %select{immediately precede declared identifier|"
  "be innermost component of anonymous pack declaration}0">;
def warn_misplaced_ellipsis_vararg : Warning<
  "'...' in this location creates a C-style varargs function"
  "%select{, not a function parameter pack|}0">,
  InGroup<DiagGroup<"ambiguous-ellipsis">>;
def note_misplaced_ellipsis_vararg_existing_ellipsis : Note<
  "preceding '...' declares a function parameter pack">;
def note_misplaced_ellipsis_vararg_add_ellipsis : Note<
  "place '...' %select{immediately before declared identifier|here}0 "
  "to declare a function parameter pack">;
def note_misplaced_ellipsis_vararg_add_comma : Note<
  "insert ',' before '...' to silence this warning">;
def ext_abstract_pack_declarator_parens : ExtWarn<
  "ISO C++11 requires a parenthesized pack declaration to have a name">,
  InGroup<DiagGroup<"anonymous-pack-parens">>;
def err_function_is_not_record : Error<
  "unexpected %0 in function call; perhaps remove the %0?">;
def err_super_in_using_declaration : Error<
  "'__super' cannot be used with a using declaration">;
def ext_constexpr_if : ExtWarn<
  "constexpr if is a C++17 extension">, InGroup<CXX17>;
def warn_cxx14_compat_constexpr_if : Warning<
  "constexpr if is incompatible with C++ standards before C++17">,
  DefaultIgnore, InGroup<CXXPre17Compat>;
def ext_init_statement : ExtWarn<
  "'%select{if|switch}0' initialization statements are a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_init_statement : Warning<
  "%select{if|switch}0 initialization statements are incompatible with "
  "C++ standards before C++17">, DefaultIgnore, InGroup<CXXPre17Compat>;
def ext_for_range_init_stmt : ExtWarn<
  "range-based for loop initialization statements are a C++2a extension">,
  InGroup<CXX2a>;
def warn_cxx17_compat_for_range_init_stmt : Warning<
  "range-based for loop initialization statements are incompatible with "
  "C++ standards before C++2a">, DefaultIgnore, InGroup<CXXPre2aCompat>;
def warn_empty_init_statement : Warning<
  "empty initialization statement of '%select{if|switch|range-based for}0' "
  "has no effect">, InGroup<EmptyInitStatement>, DefaultIgnore;

// C++ derived classes
def err_dup_virtual : Error<"duplicate 'virtual' in base specifier">;

// C++ operator overloading
def err_literal_operator_string_prefix : Error<
  "string literal after 'operator' cannot have an encoding prefix">;
def err_literal_operator_string_not_empty : Error<
  "string literal after 'operator' must be '\"\"'">;
def warn_cxx98_compat_literal_operator : Warning<
  "literal operators are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

// Classes.
def err_anon_type_definition : Error<
  "declaration of anonymous %0 must be a definition">;
def err_default_delete_in_multiple_declaration : Error<
  "'= %select{default|delete}0' is a function definition and must occur in a "
  "standalone declaration">;

def warn_cxx98_compat_noexcept_expr : Warning<
  "noexcept expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_nullptr : Warning<
  "'nullptr' is incompatible with C++98">, InGroup<CXX98Compat>, DefaultIgnore;

def warn_wrong_clang_attr_namespace : Warning<
  "'__clang__' is a predefined macro name, not an attribute scope specifier; "
  "did you mean '_Clang' instead?">, InGroup<IgnoredAttributes>;
def ext_ns_enum_attribute : Extension<
  "attributes on %select{a namespace|an enumerator}0 declaration are "
  "a C++17 extension">, InGroup<CXX17>;
def warn_cxx14_compat_ns_enum_attribute : Warning<
  "attributes on %select{a namespace|an enumerator}0 declaration are "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17CompatPedantic>, DefaultIgnore;
def warn_cxx98_compat_alignas : Warning<"'alignas' is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_attribute : Warning<
  "C++11 attribute syntax is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_cxx11_attribute_forbids_arguments : Error<
  "attribute %0 cannot have an argument list">;
def err_attribute_requires_arguments : Error<
  "parentheses must be omitted if %0 attribute's argument list is empty">;
def err_cxx11_attribute_forbids_ellipsis : Error<
  "attribute %0 cannot be used as an attribute pack">;
def err_cxx11_attribute_repeated : Error<
  "attribute %0 cannot appear multiple times in an attribute specifier">;
def warn_cxx14_compat_using_attribute_ns : Warning<
  "default scope specifier for attributes is incompatible with C++ standards "
  "before C++17">, InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_using_attribute_ns : ExtWarn<
  "default scope specifier for attributes is a C++17 extension">,
  InGroup<CXX17>;
def err_using_attribute_ns_conflict : Error<
  "attribute with scope specifier cannot follow default scope specifier">;
def err_attributes_not_allowed : Error<"an attribute list cannot appear here">;
def err_attributes_misplaced : Error<"misplaced attributes; expected attributes here">;
def err_l_square_l_square_not_attribute : Error<
  "C++11 only allows consecutive left square brackets when "
  "introducing an attribute">;
def err_ms_declspec_type : Error<
  "__declspec attributes must be an identifier or string literal">;
def err_ms_property_no_getter_or_putter : Error<
  "property does not specify a getter or a putter">;
def err_ms_property_unknown_accessor : Error<
  "expected 'get' or 'put' in property declaration">;
def err_ms_property_has_set_accessor : Error<
  "putter for property must be specified as 'put', not 'set'">;
def err_ms_property_missing_accessor_kind : Error<
  "missing 'get=' or 'put='">;
def err_ms_property_expected_equal : Error<
  "expected '=' after '%0'">;
def err_ms_property_duplicate_accessor : Error<
  "property declaration specifies '%0' accessor twice">;
def err_ms_property_expected_accessor_name : Error<
  "expected name of accessor method">;
def err_ms_property_expected_comma_or_rparen : Error<
  "expected ',' or ')' at end of property accessor list">;
def err_ms_property_initializer : Error<
  "property declaration cannot have an in-class initializer">;

/// C++ Templates
def err_expected_template : Error<"expected template">;
def err_unknown_template_name : Error<
  "unknown template name %0">;
def err_expected_comma_greater : Error<
  "expected ',' or '>' in template-parameter-list">;
def err_class_on_template_template_param : Error<
  "template template parameter requires 'class' after the parameter list">;
def ext_template_template_param_typename : ExtWarn<
  "template template parameter using 'typename' is a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_template_template_param_typename : Warning<
  "template template parameter using 'typename' is "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def err_template_spec_syntax_non_template : Error<
  "identifier followed by '<' indicates a class template specialization but "
  "%0 %select{does not refer to a template|refers to a function template|"
  "<unused>|refers to a variable template|<unused>}1">;
def err_id_after_template_in_nested_name_spec : Error<
  "expected template name after 'template' keyword in nested name specifier">;
def err_unexpected_template_in_unqualified_id : Error<
  "'template' keyword not permitted here">;
def err_two_right_angle_brackets_need_space : Error<
  "a space is required between consecutive right angle brackets (use '> >')">;
def err_right_angle_bracket_equal_needs_space : Error<
  "a space is required between a right angle bracket and an equals sign "
  "(use '> =')">;
def warn_cxx11_right_shift_in_template_arg : Warning<
  "use of right-shift operator ('>>') in template argument will require "
  "parentheses in C++11">, InGroup<CXX11Compat>;
def warn_cxx98_compat_two_right_angle_brackets : Warning<
  "consecutive right angle brackets are incompatible with C++98 (use '> >')">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_templated_invalid_declaration : Error<
  "a static_assert declaration cannot be a template">;
def err_multiple_template_declarators : Error<
  "%select{|a template declaration|an explicit template specialization|"
  "an explicit template instantiation}0 can "
  "only %select{|declare|declare|instantiate}0 a single entity">;
def err_explicit_instantiation_with_definition : Error<
  "explicit template instantiation cannot have a definition; if this "
  "definition is meant to be an explicit specialization, add '<>' after the "
  "'template' keyword">;
def err_template_defn_explicit_instantiation : Error<
  "%select{function|class|variable}0 cannot be defined in an explicit instantiation; if this "
  "declaration is meant to be a %select{function|class|variable}0 definition, remove the 'template' keyword">;
def err_friend_explicit_instantiation : Error<
  "friend cannot be declared in an explicit instantiation; if this "
  "declaration is meant to be a friend declaration, remove the 'template' keyword">;
def err_explicit_instantiation_enum : Error<
  "enumerations cannot be explicitly instantiated">;
def err_expected_template_parameter : Error<"expected template parameter">;

def err_missing_dependent_template_keyword : Error<
  "use 'template' keyword to treat '%0' as a dependent template name">;
def warn_missing_dependent_template_keyword : ExtWarn<
  "use 'template' keyword to treat '%0' as a dependent template name">;

def ext_extern_template : Extension<
  "extern templates are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_extern_template : Warning<
  "extern templates are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def warn_static_inline_explicit_inst_ignored : Warning<
  "ignoring '%select{static|inline}0' keyword on explicit template "
  "instantiation">, InGroup<DiagGroup<"static-inline-explicit-instantiation">>;

// Constructor template diagnostics.
def err_out_of_line_constructor_template_id : Error<
  "out-of-line constructor for %0 cannot have template arguments">;

def err_expected_qualified_after_typename : Error<
  "expected a qualified name after 'typename'">;
def warn_expected_qualified_after_typename : ExtWarn<
  "expected a qualified name after 'typename'">;

def err_typename_refers_to_non_type_template : Error<
  "typename specifier refers to a non-type template">;
def err_expected_type_name_after_typename : Error<
  "expected an identifier or template-id after '::'">;
def err_explicit_spec_non_template : Error<
  "explicit %select{specialization|instantiation}0 of non-template %1 %2">;

def err_default_template_template_parameter_not_template : Error<
  "default template argument for a template template parameter must be a class "
  "template">;

def ext_fold_expression : ExtWarn<
  "pack fold expression is a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_fold_expression : Warning<
  "pack fold expression is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def err_expected_fold_operator : Error<
  "expected a foldable binary operator in fold expression">;
def err_fold_operator_mismatch : Error<
  "operators in fold expression must be the same">;

def err_ctor_init_missing_comma : Error<
  "missing ',' between base or member initializers">;

// C++ declarations
def err_friend_decl_defines_type : Error<
  "cannot define a type in a friend declaration">;
def err_missing_whitespace_digraph : Error<
  "found '<::' after a "
  "%select{template name|const_cast|dynamic_cast|reinterpret_cast|static_cast}0"
  " which forms the digraph '<:' (aka '[') and a ':', did you mean '< ::'?">;

def ext_defaulted_deleted_function : ExtWarn<
  "%select{defaulted|deleted}0 function definitions are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_defaulted_deleted_function : Warning<
  "%select{defaulted|deleted}0 function definitions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

// C++11 in-class member initialization
def ext_nonstatic_member_init : ExtWarn<
  "in-class initialization of non-static data member is a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_nonstatic_member_init : Warning<
  "in-class initialization of non-static data members is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_bitfield_member_init: ExtWarn<
  "default member initializer for bit-field is a C++2a extension">,
  InGroup<CXX2a>;
def warn_cxx17_compat_bitfield_member_init: Warning<
  "default member initializer for bit-field is incompatible with "
  "C++ standards before C++2a">, InGroup<CXXPre2aCompat>, DefaultIgnore;
def err_incomplete_array_member_init: Error<
  "array bound cannot be deduced from an in-class initializer">;

// C++11 alias-declaration
def ext_alias_declaration : ExtWarn<
  "alias declarations are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_alias_declaration : Warning<
  "alias declarations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_alias_declaration_not_identifier : Error<
  "name defined in alias declaration must be an identifier">;
def err_alias_declaration_specialization : Error<
  "%select{partial specialization|explicit specialization|explicit instantiation}0 of alias templates is not permitted">;
def err_alias_declaration_pack_expansion : Error<
  "alias declaration cannot be a pack expansion">;

// C++17 using-declaration pack expansions
def ext_multi_using_declaration : ExtWarn<
  "use of multiple declarators in a single using declaration is "
  "a C++17 extension">, InGroup<CXX17>;
def warn_cxx17_compat_multi_using_declaration : Warning<
  "use of multiple declarators in a single using declaration is "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_using_declaration_pack : ExtWarn<
  "pack expansion of using declaration is a C++17 extension">, InGroup<CXX17>;
def warn_cxx17_compat_using_declaration_pack : Warning<
  "pack expansion using declaration is incompatible with C++ standards "
  "before C++17">, InGroup<CXXPre17Compat>, DefaultIgnore;

// C++11 override control
def ext_override_control_keyword : ExtWarn<
  "'%0' keyword is a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_override_control_keyword : Warning<
  "'%0' keyword is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_override_control_interface : Error<
  "'%0' keyword not permitted with interface types">;
def ext_ms_sealed_keyword : ExtWarn<
  "'sealed' keyword is a Microsoft extension">,
  InGroup<MicrosoftSealed>;

def err_access_specifier_interface : Error<
  "interface types cannot specify '%select{private|protected}0' access">;

def err_duplicate_virt_specifier : Error<
  "class member already marked '%0'">;

def err_scoped_enum_missing_identifier : Error<
  "scoped enumeration requires a name">;
def ext_scoped_enum : ExtWarn<
  "scoped enumerations are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_scoped_enum : Warning<
  "scoped enumerations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

def err_expected_parameter_pack : Error<
  "expected the name of a parameter pack">;
def err_paren_sizeof_parameter_pack : Error<
  "missing parentheses around the size of parameter pack %0">;
def err_sizeof_parameter_pack : Error<
  "expected parenthesized parameter pack name in 'sizeof...' expression">;

// C++11 lambda expressions
def err_expected_comma_or_rsquare : Error<
  "expected ',' or ']' in lambda capture list">;
def err_this_captured_by_reference : Error<
  "'this' cannot be captured by reference">;
def err_expected_capture : Error<
  "expected variable name or 'this' in lambda capture list">;
def err_expected_lambda_body : Error<"expected body of lambda expression">;
def warn_cxx98_compat_lambda : Warning<
  "lambda expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_lambda_missing_parens : Error<
  "lambda requires '()' before %select{'mutable'|return type|"
  "attribute specifier|'constexpr'}0">;
def err_lambda_decl_specifier_repeated : Error<
  "%select{'mutable'|'constexpr'}0 cannot appear multiple times in a lambda declarator">;
// C++17 lambda expressions
def err_expected_star_this_capture : Error<
  "expected 'this' following '*' in lambda capture list">;

// C++17 constexpr lambda expressions
def warn_cxx14_compat_constexpr_on_lambda : Warning<
  "constexpr on lambda expressions is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_constexpr_on_lambda_cxx17 : ExtWarn<
  "'constexpr' on lambda expressions is a C++17 extension">, InGroup<CXX17>;

// Availability attribute
def err_expected_version : Error<
  "expected a version of the form 'major[.minor[.subminor]]'">;
def warn_expected_consistent_version_separator : Warning<
  "use same version number separators '_' or '.'; as in "
  "'major[.minor[.subminor]]'">, InGroup<Availability>;
def err_zero_version : Error<
  "version number must have non-zero major, minor, or sub-minor version">;
def err_availability_expected_platform : Error<
  "expected a platform name, e.g., 'macos'">;

// objc_bridge_related attribute
def err_objcbridge_related_expected_related_class : Error<
  "expected a related ObjectiveC class name, e.g., 'NSColor'">;
def err_objcbridge_related_selector_name : Error<
  "expected a class method selector with single argument, e.g., 'colorWithCGColor:'">;

def err_availability_expected_change : Error<
  "expected 'introduced', 'deprecated', or 'obsoleted'">;
def err_availability_unknown_change : Error<
  "%0 is not an availability stage; use 'introduced', 'deprecated', or "
  "'obsoleted'">;
def err_availability_redundant : Error<
  "redundant %0 availability change; only the last specified change will "
  "be used">;
def warn_availability_and_unavailable : Warning<
  "'unavailable' availability overrides all other availability information">,
  InGroup<Availability>;

// @available(...)
def err_avail_query_expected_platform_name : Error<
  "expected a platform name here">;

def err_avail_query_unrecognized_platform_name : Error<
  "unrecognized platform name %0">;
def err_availability_query_wildcard_required: Error<
  "must handle potential future platforms with '*'">;
def err_availability_query_repeated_platform: Error<
  "version for '%0' already specified">;
def err_availability_query_repeated_star : Error<
  "'*' query has already been specified">;

// External source symbol attribute
def err_external_source_symbol_expected_keyword : Error<
  "expected 'language', 'defined_in', or 'generated_declaration'">;
def err_external_source_symbol_duplicate_clause : Error<
  "duplicate %0 clause in an 'external_source_symbol' attribute">;

// Type safety attributes
def err_type_safety_unknown_flag : Error<
  "invalid comparison flag %0; use 'layout_compatible' or 'must_be_null'">;

// Type traits
def err_type_trait_arity : Error<
  "type trait requires %0%select{| or more}1 argument%select{|s}2; have "
  "%3 argument%s3">;

// Language specific pragmas
// - Generic warnings
def warn_pragma_expected_lparen : Warning<
  "missing '(' after '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_rparen : Warning<
  "missing ')' after '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_identifier : Warning<
  "expected identifier in '#pragma %0' - ignored">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_string : Warning<
  "expected string literal in '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_missing_argument : Warning<
  "missing argument to '#pragma %0'%select{|; expected %2}1">, InGroup<IgnoredPragmas>;
def warn_pragma_invalid_argument : Warning<
  "unexpected argument '%0' to '#pragma %1'%select{|; expected %3}2">, InGroup<IgnoredPragmas>;

// '#pragma clang section' related errors
def err_pragma_expected_clang_section_name : Error<
  "expected one of [bss|data|rodata|text] section kind in '#pragma %0'">;
def err_pragma_clang_section_expected_equal : Error<
  "expected '=' following '#pragma clang section %select{invalid|bss|data|rodata|text}0'">;
def warn_pragma_expected_section_name : Warning<
  "expected a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;

def warn_pragma_expected_section_push_pop_or_name : Warning<
  "expected push, pop or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_section_label_or_name : Warning<
  "expected a stack label or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_init_seg : Warning<
  "expected 'compiler', 'lib', 'user', or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_integer : Warning<
  "expected integer between %0 and %1 inclusive in '#pragma %2' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_ms_struct : Warning<
  "incorrect use of '#pragma ms_struct on|off' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_extra_tokens_at_eol : Warning<
  "extra tokens at end of '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_comma : Warning<
  "expected ',' in '#pragma %0'">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_punc : Warning<
  "expected ')' or ',' in '#pragma %0'">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_non_wide_string : Warning<
  "expected non-wide string literal in '#pragma %0'">, InGroup<IgnoredPragmas>;
// - Generic errors
def err_pragma_missing_argument : Error<
  "missing argument to '#pragma %0'%select{|; expected %2}1">;
// - #pragma options
def warn_pragma_options_expected_align : Warning<
  "expected 'align' following '#pragma options' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_align_expected_equal : Warning<
  "expected '=' following '#pragma %select{align|options align}0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_align_invalid_option : Warning<
  "invalid alignment option in '#pragma %select{align|options align}0' - ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma pack
def warn_pragma_unsupported_action : Warning<
  "known but unsupported action '%1' for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_invalid_specific_action : Warning<
  "unknown action '%1' for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_action_or_r_paren : Warning<
  "expected action or ')' in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_invalid_action : Warning<
  "unknown action for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_pack_malformed : Warning<
  "expected integer or identifier in '#pragma pack' - ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma intrinsic
def warn_pragma_intrinsic_builtin : Warning<
  "%0 is not a recognized builtin%select{|; consider including <intrin.h> to access non-builtin intrinsics}1">,
  InGroup<IgnoredPragmaIntrinsic>;
// - #pragma optimize
def warn_pragma_optimize : Warning<
  "'#pragma optimize' is not supported">,
  InGroup<IgnoredPragmaOptimize>;
// - #pragma unused
def warn_pragma_unused_expected_var : Warning<
  "expected '#pragma unused' argument to be a variable name">,
  InGroup<IgnoredPragmas>;
// - #pragma init_seg
def warn_pragma_init_seg_unsupported_target : Warning<
  "'#pragma init_seg' is only supported when targeting a "
  "Microsoft environment">,
  InGroup<IgnoredPragmas>;
// - #pragma fp_contract
def err_pragma_fp_contract_scope : Error<
  "'#pragma fp_contract' can only appear at file scope or at the start of a "
  "compound statement">;
// - #pragma stdc unknown
def ext_stdc_pragma_ignored : ExtWarn<"unknown pragma in STDC namespace">,
   InGroup<UnknownPragmas>;
def warn_stdc_fenv_access_not_supported :
   Warning<"pragma STDC FENV_ACCESS ON is not supported, ignoring pragma">,
   InGroup<UnknownPragmas>;
// - #pragma comment
def err_pragma_comment_malformed : Error<
  "pragma comment requires parenthesized identifier and optional string">;
def err_pragma_comment_unknown_kind : Error<"unknown kind of pragma comment">;
// PS4 recognizes only #pragma comment(lib)
def warn_pragma_comment_ignored : Warning<"'#pragma comment %0' ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma detect_mismatch
def err_pragma_detect_mismatch_malformed : Error<
  "pragma detect_mismatch is malformed; it requires two comma-separated "
  "string literals">;
// - #pragma pointers_to_members
def err_pragma_pointers_to_members_unknown_kind : Error<
  "unexpected %0, expected to see one of %select{|'best_case', 'full_generality', }1"
  "'single_inheritance', 'multiple_inheritance', or 'virtual_inheritance'">;
// - #pragma clang optimize on/off
def err_pragma_optimize_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang optimize'; "
  "expected 'on' or 'off'">;
def err_pragma_optimize_extra_argument : Error<
  "unexpected extra argument '%0' to '#pragma clang optimize'">;
// - #pragma clang attribute
def err_pragma_attribute_expected_push_pop_paren : Error<
  "expected 'push', 'pop', or '(' after '#pragma clang attribute'">;
def err_pragma_attribute_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang attribute'; "
  "expected 'push' or 'pop'">;
def err_pragma_attribute_expected_attribute : Error<
  "expected an attribute after '('">;
def err_pragma_attribute_expected_attribute_name : Error<
  "expected identifier that represents an attribute name">;
def err_pragma_attribute_extra_tokens_after_attribute : Error<
  "extra tokens after attribute in a '#pragma clang attribute push'">;
def err_pragma_attribute_unsupported_attribute : Error<
  "attribute %0 is not supported by '#pragma clang attribute'">;
def err_pragma_attribute_multiple_attributes : Error<
  "more than one attribute specified in '#pragma clang attribute push'">;
def err_pragma_attribute_expected_attribute_syntax : Error<
  "expected an attribute that is specified using the GNU, C++11 or '__declspec'"
  " syntax">;
def note_pragma_attribute_use_attribute_kw : Note<"use the GNU '__attribute__' "
  "syntax">;
def err_pragma_attribute_invalid_subject_set_specifier : Error<
  "expected attribute subject set specifier 'apply_to'">;
def err_pragma_attribute_expected_subject_identifier : Error<
  "expected an identifier that corresponds to an attribute subject rule">;
def err_pragma_attribute_unknown_subject_rule : Error<
  "unknown attribute subject rule '%0'">;
def err_pragma_attribute_expected_subject_sub_identifier : Error<
  "expected an identifier that corresponds to an attribute subject matcher "
  "sub-rule; '%0' matcher %select{does not support sub-rules|supports the "
  "following sub-rules: %2|}1">;
def err_pragma_attribute_unknown_subject_sub_rule : Error<
  "%select{invalid use of|unknown}2 attribute subject matcher sub-rule '%0'; "
  "'%1' matcher %select{does not support sub-rules|supports the following "
  "sub-rules: %3}2">;
def err_pragma_attribute_duplicate_subject : Error<
  "duplicate attribute subject matcher '%0'">;
def err_pragma_attribute_expected_period : Error<
  "expected '.' after pragma attribute namespace %0">;
def err_pragma_attribute_namespace_on_attribute : Error<
  "namespace can only apply to 'push' or 'pop' directives">;
def note_pragma_attribute_namespace_on_attribute : Note<
  "omit the namespace to add attributes to the most-recently"
  " pushed attribute group">;

def err_opencl_unroll_hint_on_non_loop : Error<
  "OpenCL only supports 'opencl_unroll_hint' attribute on for, while, and do statements">;

// OpenCL EXTENSION pragma (OpenCL 1.1 [9.1])
def warn_pragma_expected_colon : Warning<
  "missing ':' after %0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_predicate : Warning<
  "expected %select{'enable', 'disable', 'begin' or 'end'|'disable'}0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_begin_end_mismatch : Warning<
  "OpenCL extension end directive mismatches begin directive - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_unknown_extension : Warning<
  "unknown OpenCL extension %0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_unsupported_extension : Warning<
  "unsupported OpenCL extension %0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_extension_is_core : Warning<
  "OpenCL extension %0 is core feature or supported optional core feature - ignoring">, InGroup<DiagGroup<"pedantic-core-features">>, DefaultIgnore;

// OpenCL errors.
def err_opencl_taking_function_address_parser : Error<
  "taking address of function is not allowed">;
def err_opencl_logical_exclusive_or : Error<
  "^^ is a reserved operator in OpenCL">;

// OpenCL C++.
def err_openclcxx_virtual_function : Error<
  "virtual functions are not supported in OpenCL C++">;
def err_openclcxx_reserved : Error<
  "'%0' is a reserved keyword in OpenCL C++">;

// OpenMP support.
def warn_pragma_omp_ignored : Warning<
  "unexpected '#pragma omp ...' in program">, InGroup<SourceUsesOpenMP>, DefaultIgnore;
def warn_omp_extra_tokens_at_eol : Warning<
  "extra tokens at the end of '#pragma omp %0' are ignored">,
  InGroup<ExtraTokens>;
def warn_pragma_expected_colon_r_paren : Warning<
  "missing ':' or ')' after %0 - ignoring">, InGroup<IgnoredPragmas>;
def err_omp_unknown_directive : Error<
  "expected an OpenMP directive">;
def err_omp_unexpected_directive : Error<
  "unexpected OpenMP directive %select{|'#pragma omp %1'}0">;
def err_omp_expected_punc : Error<
  "expected ',' or ')' in '%0' %select{clause|directive}1">;
def err_omp_unexpected_clause : Error<
  "unexpected OpenMP clause '%0' in directive '#pragma omp %1'">;
def err_omp_immediate_directive : Error<
  "'#pragma omp %0' %select{|with '%2' clause }1cannot be an immediate substatement">;
def err_omp_expected_identifier_for_critical : Error<
  "expected identifier specifying the name of the 'omp critical' directive">;
def err_omp_expected_reduction_identifier : Error<
  "expected identifier or one of the following operators: '+', '-', '*', '&', '|', '^', '&&', or '||'">;
def err_omp_decl_in_declare_simd : Error<
  "function declaration is expected after 'declare simd' directive">;
def err_omp_unknown_map_type : Error<
  "incorrect map type, expected one of 'to', 'from', 'tofrom', 'alloc', 'release', or 'delete'">;
def err_omp_unknown_map_type_modifier : Error<
  "incorrect map type modifier, expected 'always' or 'close'">;
def err_omp_map_type_missing : Error<
  "missing map type">;
def err_omp_map_type_modifier_missing : Error<
  "missing map type modifier">;
def err_omp_declare_simd_inbranch_notinbranch : Error<
  "unexpected '%0' clause, '%1' is specified already">;
def err_expected_end_declare_target : Error<
  "expected '#pragma omp end declare target'">;
def err_omp_declare_target_unexpected_clause: Error<
  "unexpected '%0' clause, only 'to' or 'link' clauses expected">;
def err_omp_expected_clause: Error<
  "expected at least one clause on '#pragma omp %0' directive">;

// Pragma loop support.
def err_pragma_loop_missing_argument : Error<
  "missing argument; expected %select{an integer value|"
  "'enable'%select{|, 'full'}1%select{|, 'assume_safety'}2 or 'disable'}0">;
def err_pragma_loop_invalid_option : Error<
  "%select{invalid|missing}0 option%select{ %1|}0; expected vectorize, "
  "vectorize_width, interleave, interleave_count, unroll, unroll_count, "
  "pipeline, pipeline_initiation_interval, or distribute">;

def err_pragma_fp_invalid_option : Error<
  "%select{invalid|missing}0 option%select{ %1|}0; expected contract">;
def err_pragma_fp_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang fp %1'; "
  "expected 'on', 'fast' or 'off'">;
def err_pragma_fp_scope : Error<
  "'#pragma clang fp' can only appear at file scope or at the start of a "
  "compound statement">;

def err_pragma_invalid_keyword : Error<
  "invalid argument; expected 'enable'%select{|, 'full'}0%select{|, 'assume_safety'}1 or 'disable'">;
def err_pragma_pipeline_invalid_keyword : Error<
    "invalid argument; expected 'disable'">;

// Pragma unroll support.
def warn_pragma_unroll_cuda_value_in_parens : Warning<
  "argument to '#pragma unroll' should not be in parentheses in CUDA C/C++">,
  InGroup<CudaCompat>;

def warn_cuda_attr_lambda_position : Warning<
  "nvcc does not allow '__%0__' to appear after '()' in lambdas">,
  InGroup<CudaCompat>;
def warn_pragma_force_cuda_host_device_bad_arg : Warning<
  "incorrect use of #pragma clang force_cuda_host_device begin|end">,
  InGroup<IgnoredPragmas>;
def err_pragma_cannot_end_force_cuda_host_device : Error<
  "force_cuda_host_device end pragma without matching "
  "force_cuda_host_device begin">;
} // end of Parse Issue category.

let CategoryName = "Modules Issue" in {
def err_unexpected_module_decl : Error<
  "module declaration can only appear at the top level">;
def err_module_expected_ident : Error<
  "expected a module name after '%select{module|import}0'">;
def err_module_implementation_partition : Error<
  "module partition must be declared 'export'">;
def err_attribute_not_module_attr : Error<
  "%0 attribute cannot be applied to a module">;
def err_attribute_not_import_attr : Error<
  "%0 attribute cannot be applied to a module import">;
def err_module_expected_semi : Error<
  "expected ';' after module name">;
def err_missing_before_module_end : Error<"expected %0 at end of module">;

def err_export_empty : Error<"export declaration cannot be empty">;
}

let CategoryName = "Generics Issue" in {

def err_objc_expected_type_parameter : Error<
  "expected type parameter name">;

def err_objc_parameterized_implementation : Error<
  "@implementation cannot have type parameters">;

def err_objc_type_args_after_protocols : Error<
  "protocol qualifiers must precede type arguments">;

def note_meant_to_use_typename : Note<
  "did you mean to use 'typename'?">;
}

let CategoryName = "Coroutines Issue" in {
def err_for_co_await_not_range_for : Error<
  "'co_await' modifier can only be applied to range-based for loop">;
}

} // end of Parser diagnostics
