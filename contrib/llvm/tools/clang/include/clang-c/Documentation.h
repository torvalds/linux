/*==-- clang-c/Documentation.h - Utilities for comment processing -*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a supplementary interface for inspecting              *|
|* documentation comments.                                                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_DOCUMENTATION_H
#define LLVM_CLANG_C_DOCUMENTATION_H

#include "clang-c/Index.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup CINDEX_COMMENT Comment introspection
 *
 * The routines in this group provide access to information in documentation
 * comments. These facilities are distinct from the core and may be subject to
 * their own schedule of stability and deprecation.
 *
 * @{
 */

/**
 * A parsed comment.
 */
typedef struct {
  const void *ASTNode;
  CXTranslationUnit TranslationUnit;
} CXComment;

/**
 * Given a cursor that represents a documentable entity (e.g.,
 * declaration), return the associated parsed comment as a
 * \c CXComment_FullComment AST node.
 */
CINDEX_LINKAGE CXComment clang_Cursor_getParsedComment(CXCursor C);

/**
 * Describes the type of the comment AST node (\c CXComment).  A comment
 * node can be considered block content (e. g., paragraph), inline content
 * (plain text) or neither (the root AST node).
 */
enum CXCommentKind {
  /**
   * Null comment.  No AST node is constructed at the requested location
   * because there is no text or a syntax error.
   */
  CXComment_Null = 0,

  /**
   * Plain text.  Inline content.
   */
  CXComment_Text = 1,

  /**
   * A command with word-like arguments that is considered inline content.
   *
   * For example: \\c command.
   */
  CXComment_InlineCommand = 2,

  /**
   * HTML start tag with attributes (name-value pairs).  Considered
   * inline content.
   *
   * For example:
   * \verbatim
   * <br> <br /> <a href="http://example.org/">
   * \endverbatim
   */
  CXComment_HTMLStartTag = 3,

  /**
   * HTML end tag.  Considered inline content.
   *
   * For example:
   * \verbatim
   * </a>
   * \endverbatim
   */
  CXComment_HTMLEndTag = 4,

  /**
   * A paragraph, contains inline comment.  The paragraph itself is
   * block content.
   */
  CXComment_Paragraph = 5,

  /**
   * A command that has zero or more word-like arguments (number of
   * word-like arguments depends on command name) and a paragraph as an
   * argument.  Block command is block content.
   *
   * Paragraph argument is also a child of the block command.
   *
   * For example: \has 0 word-like arguments and a paragraph argument.
   *
   * AST nodes of special kinds that parser knows about (e. g., \\param
   * command) have their own node kinds.
   */
  CXComment_BlockCommand = 6,

  /**
   * A \\param or \\arg command that describes the function parameter
   * (name, passing direction, description).
   *
   * For example: \\param [in] ParamName description.
   */
  CXComment_ParamCommand = 7,

  /**
   * A \\tparam command that describes a template parameter (name and
   * description).
   *
   * For example: \\tparam T description.
   */
  CXComment_TParamCommand = 8,

  /**
   * A verbatim block command (e. g., preformatted code).  Verbatim
   * block has an opening and a closing command and contains multiple lines of
   * text (\c CXComment_VerbatimBlockLine child nodes).
   *
   * For example:
   * \\verbatim
   * aaa
   * \\endverbatim
   */
  CXComment_VerbatimBlockCommand = 9,

  /**
   * A line of text that is contained within a
   * CXComment_VerbatimBlockCommand node.
   */
  CXComment_VerbatimBlockLine = 10,

  /**
   * A verbatim line command.  Verbatim line has an opening command,
   * a single line of text (up to the newline after the opening command) and
   * has no closing command.
   */
  CXComment_VerbatimLine = 11,

  /**
   * A full comment attached to a declaration, contains block content.
   */
  CXComment_FullComment = 12
};

/**
 * The most appropriate rendering mode for an inline command, chosen on
 * command semantics in Doxygen.
 */
enum CXCommentInlineCommandRenderKind {
  /**
   * Command argument should be rendered in a normal font.
   */
  CXCommentInlineCommandRenderKind_Normal,

  /**
   * Command argument should be rendered in a bold font.
   */
  CXCommentInlineCommandRenderKind_Bold,

  /**
   * Command argument should be rendered in a monospaced font.
   */
  CXCommentInlineCommandRenderKind_Monospaced,

  /**
   * Command argument should be rendered emphasized (typically italic
   * font).
   */
  CXCommentInlineCommandRenderKind_Emphasized
};

/**
 * Describes parameter passing direction for \\param or \\arg command.
 */
enum CXCommentParamPassDirection {
  /**
   * The parameter is an input parameter.
   */
  CXCommentParamPassDirection_In,

  /**
   * The parameter is an output parameter.
   */
  CXCommentParamPassDirection_Out,

  /**
   * The parameter is an input and output parameter.
   */
  CXCommentParamPassDirection_InOut
};

/**
 * \param Comment AST node of any kind.
 *
 * \returns the type of the AST node.
 */
CINDEX_LINKAGE enum CXCommentKind clang_Comment_getKind(CXComment Comment);

/**
 * \param Comment AST node of any kind.
 *
 * \returns number of children of the AST node.
 */
CINDEX_LINKAGE unsigned clang_Comment_getNumChildren(CXComment Comment);

/**
 * \param Comment AST node of any kind.
 *
 * \param ChildIdx child index (zero-based).
 *
 * \returns the specified child of the AST node.
 */
CINDEX_LINKAGE
CXComment clang_Comment_getChild(CXComment Comment, unsigned ChildIdx);

/**
 * A \c CXComment_Paragraph node is considered whitespace if it contains
 * only \c CXComment_Text nodes that are empty or whitespace.
 *
 * Other AST nodes (except \c CXComment_Paragraph and \c CXComment_Text) are
 * never considered whitespace.
 *
 * \returns non-zero if \c Comment is whitespace.
 */
CINDEX_LINKAGE unsigned clang_Comment_isWhitespace(CXComment Comment);

/**
 * \returns non-zero if \c Comment is inline content and has a newline
 * immediately following it in the comment text.  Newlines between paragraphs
 * do not count.
 */
CINDEX_LINKAGE
unsigned clang_InlineContentComment_hasTrailingNewline(CXComment Comment);

/**
 * \param Comment a \c CXComment_Text AST node.
 *
 * \returns text contained in the AST node.
 */
CINDEX_LINKAGE CXString clang_TextComment_getText(CXComment Comment);

/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns name of the inline command.
 */
CINDEX_LINKAGE
CXString clang_InlineCommandComment_getCommandName(CXComment Comment);

/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns the most appropriate rendering mode, chosen on command
 * semantics in Doxygen.
 */
CINDEX_LINKAGE enum CXCommentInlineCommandRenderKind
clang_InlineCommandComment_getRenderKind(CXComment Comment);

/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns number of command arguments.
 */
CINDEX_LINKAGE
unsigned clang_InlineCommandComment_getNumArgs(CXComment Comment);

/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \param ArgIdx argument index (zero-based).
 *
 * \returns text of the specified argument.
 */
CINDEX_LINKAGE
CXString clang_InlineCommandComment_getArgText(CXComment Comment,
                                               unsigned ArgIdx);

/**
 * \param Comment a \c CXComment_HTMLStartTag or \c CXComment_HTMLEndTag AST
 * node.
 *
 * \returns HTML tag name.
 */
CINDEX_LINKAGE CXString clang_HTMLTagComment_getTagName(CXComment Comment);

/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \returns non-zero if tag is self-closing (for example, &lt;br /&gt;).
 */
CINDEX_LINKAGE
unsigned clang_HTMLStartTagComment_isSelfClosing(CXComment Comment);

/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \returns number of attributes (name-value pairs) attached to the start tag.
 */
CINDEX_LINKAGE unsigned clang_HTMLStartTag_getNumAttrs(CXComment Comment);

/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \param AttrIdx attribute index (zero-based).
 *
 * \returns name of the specified attribute.
 */
CINDEX_LINKAGE
CXString clang_HTMLStartTag_getAttrName(CXComment Comment, unsigned AttrIdx);

/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \param AttrIdx attribute index (zero-based).
 *
 * \returns value of the specified attribute.
 */
CINDEX_LINKAGE
CXString clang_HTMLStartTag_getAttrValue(CXComment Comment, unsigned AttrIdx);

/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \returns name of the block command.
 */
CINDEX_LINKAGE
CXString clang_BlockCommandComment_getCommandName(CXComment Comment);

/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \returns number of word-like arguments.
 */
CINDEX_LINKAGE
unsigned clang_BlockCommandComment_getNumArgs(CXComment Comment);

/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \param ArgIdx argument index (zero-based).
 *
 * \returns text of the specified word-like argument.
 */
CINDEX_LINKAGE
CXString clang_BlockCommandComment_getArgText(CXComment Comment,
                                              unsigned ArgIdx);

/**
 * \param Comment a \c CXComment_BlockCommand or
 * \c CXComment_VerbatimBlockCommand AST node.
 *
 * \returns paragraph argument of the block command.
 */
CINDEX_LINKAGE
CXComment clang_BlockCommandComment_getParagraph(CXComment Comment);

/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns parameter name.
 */
CINDEX_LINKAGE
CXString clang_ParamCommandComment_getParamName(CXComment Comment);

/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns non-zero if the parameter that this AST node represents was found
 * in the function prototype and \c clang_ParamCommandComment_getParamIndex
 * function will return a meaningful value.
 */
CINDEX_LINKAGE
unsigned clang_ParamCommandComment_isParamIndexValid(CXComment Comment);

/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns zero-based parameter index in function prototype.
 */
CINDEX_LINKAGE
unsigned clang_ParamCommandComment_getParamIndex(CXComment Comment);

/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns non-zero if parameter passing direction was specified explicitly in
 * the comment.
 */
CINDEX_LINKAGE
unsigned clang_ParamCommandComment_isDirectionExplicit(CXComment Comment);

/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns parameter passing direction.
 */
CINDEX_LINKAGE
enum CXCommentParamPassDirection clang_ParamCommandComment_getDirection(
                                                            CXComment Comment);

/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns template parameter name.
 */
CINDEX_LINKAGE
CXString clang_TParamCommandComment_getParamName(CXComment Comment);

/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns non-zero if the parameter that this AST node represents was found
 * in the template parameter list and
 * \c clang_TParamCommandComment_getDepth and
 * \c clang_TParamCommandComment_getIndex functions will return a meaningful
 * value.
 */
CINDEX_LINKAGE
unsigned clang_TParamCommandComment_isParamPositionValid(CXComment Comment);

/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns zero-based nesting depth of this parameter in the template parameter list.
 *
 * For example,
 * \verbatim
 *     template<typename C, template<typename T> class TT>
 *     void test(TT<int> aaa);
 * \endverbatim
 * for C and TT nesting depth is 0,
 * for T nesting depth is 1.
 */
CINDEX_LINKAGE
unsigned clang_TParamCommandComment_getDepth(CXComment Comment);

/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns zero-based parameter index in the template parameter list at a
 * given nesting depth.
 *
 * For example,
 * \verbatim
 *     template<typename C, template<typename T> class TT>
 *     void test(TT<int> aaa);
 * \endverbatim
 * for C and TT nesting depth is 0, so we can ask for index at depth 0:
 * at depth 0 C's index is 0, TT's index is 1.
 *
 * For T nesting depth is 1, so we can ask for index at depth 0 and 1:
 * at depth 0 T's index is 1 (same as TT's),
 * at depth 1 T's index is 0.
 */
CINDEX_LINKAGE
unsigned clang_TParamCommandComment_getIndex(CXComment Comment, unsigned Depth);

/**
 * \param Comment a \c CXComment_VerbatimBlockLine AST node.
 *
 * \returns text contained in the AST node.
 */
CINDEX_LINKAGE
CXString clang_VerbatimBlockLineComment_getText(CXComment Comment);

/**
 * \param Comment a \c CXComment_VerbatimLine AST node.
 *
 * \returns text contained in the AST node.
 */
CINDEX_LINKAGE CXString clang_VerbatimLineComment_getText(CXComment Comment);

/**
 * Convert an HTML tag AST node to string.
 *
 * \param Comment a \c CXComment_HTMLStartTag or \c CXComment_HTMLEndTag AST
 * node.
 *
 * \returns string containing an HTML tag.
 */
CINDEX_LINKAGE CXString clang_HTMLTagComment_getAsString(CXComment Comment);

/**
 * Convert a given full parsed comment to an HTML fragment.
 *
 * Specific details of HTML layout are subject to change.  Don't try to parse
 * this HTML back into an AST, use other APIs instead.
 *
 * Currently the following CSS classes are used:
 * \li "para-brief" for \paragraph and equivalent commands;
 * \li "para-returns" for \\returns paragraph and equivalent commands;
 * \li "word-returns" for the "Returns" word in \\returns paragraph.
 *
 * Function argument documentation is rendered as a \<dl\> list with arguments
 * sorted in function prototype order.  CSS classes used:
 * \li "param-name-index-NUMBER" for parameter name (\<dt\>);
 * \li "param-descr-index-NUMBER" for parameter description (\<dd\>);
 * \li "param-name-index-invalid" and "param-descr-index-invalid" are used if
 * parameter index is invalid.
 *
 * Template parameter documentation is rendered as a \<dl\> list with
 * parameters sorted in template parameter list order.  CSS classes used:
 * \li "tparam-name-index-NUMBER" for parameter name (\<dt\>);
 * \li "tparam-descr-index-NUMBER" for parameter description (\<dd\>);
 * \li "tparam-name-index-other" and "tparam-descr-index-other" are used for
 * names inside template template parameters;
 * \li "tparam-name-index-invalid" and "tparam-descr-index-invalid" are used if
 * parameter position is invalid.
 *
 * \param Comment a \c CXComment_FullComment AST node.
 *
 * \returns string containing an HTML fragment.
 */
CINDEX_LINKAGE CXString clang_FullComment_getAsHTML(CXComment Comment);

/**
 * Convert a given full parsed comment to an XML document.
 *
 * A Relax NG schema for the XML can be found in comment-xml-schema.rng file
 * inside clang source tree.
 *
 * \param Comment a \c CXComment_FullComment AST node.
 *
 * \returns string containing an XML document.
 */
CINDEX_LINKAGE CXString clang_FullComment_getAsXML(CXComment Comment);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif

#endif /* CLANG_C_DOCUMENTATION_H */

