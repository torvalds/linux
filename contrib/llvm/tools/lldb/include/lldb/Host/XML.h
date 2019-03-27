//===-- XML.h ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_XML_h_
#define liblldb_XML_h_

#if defined(LIBXML2_DEFINED)
#include <libxml/xmlreader.h>
#endif

#include <functional>
#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"

#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

#if defined(LIBXML2_DEFINED)
typedef xmlNodePtr XMLNodeImpl;
typedef xmlDocPtr XMLDocumentImpl;
#else
typedef void *XMLNodeImpl;
typedef void *XMLDocumentImpl;
#endif

class XMLNode;

typedef std::vector<std::string> NamePath;
typedef std::function<bool(const XMLNode &node)> NodeCallback;
typedef std::function<bool(const llvm::StringRef &name,
                           const llvm::StringRef &value)>
    AttributeCallback;

class XMLNode {
public:
  XMLNode();

  XMLNode(XMLNodeImpl node);

  ~XMLNode();

  explicit operator bool() const { return IsValid(); }

  void Clear();

  bool IsValid() const;

  bool IsElement() const;

  llvm::StringRef GetName() const;

  bool GetElementText(std::string &text) const;

  bool GetElementTextAsUnsigned(uint64_t &value, uint64_t fail_value = 0,
                                int base = 0) const;

  bool GetElementTextAsFloat(double &value, double fail_value = 0.0) const;

  bool NameIs(const char *name) const;

  XMLNode GetParent() const;

  XMLNode GetSibling() const;

  XMLNode GetChild() const;

  llvm::StringRef GetAttributeValue(const char *name,
                                    const char *fail_value = nullptr) const;

  bool GetAttributeValueAsUnsigned(const char *name, uint64_t &value,
                                   uint64_t fail_value = 0, int base = 0) const;

  XMLNode FindFirstChildElementWithName(const char *name) const;

  XMLNode GetElementForPath(const NamePath &path);

  //----------------------------------------------------------------------
  // Iterate through all sibling nodes of any type
  //----------------------------------------------------------------------
  void ForEachSiblingNode(NodeCallback const &callback) const;

  //----------------------------------------------------------------------
  // Iterate through only the sibling nodes that are elements
  //----------------------------------------------------------------------
  void ForEachSiblingElement(NodeCallback const &callback) const;

  //----------------------------------------------------------------------
  // Iterate through only the sibling nodes that are elements and whose name
  // matches \a name.
  //----------------------------------------------------------------------
  void ForEachSiblingElementWithName(const char *name,
                                     NodeCallback const &callback) const;

  void ForEachChildNode(NodeCallback const &callback) const;

  void ForEachChildElement(NodeCallback const &callback) const;

  void ForEachChildElementWithName(const char *name,
                                   NodeCallback const &callback) const;

  void ForEachAttribute(AttributeCallback const &callback) const;

protected:
  XMLNodeImpl m_node;
};

class XMLDocument {
public:
  XMLDocument();

  ~XMLDocument();

  explicit operator bool() const { return IsValid(); }

  bool IsValid() const;

  void Clear();

  bool ParseFile(const char *path);

  bool ParseMemory(const char *xml, size_t xml_length,
                   const char *url = "untitled.xml");

  //----------------------------------------------------------------------
  // If \a name is nullptr, just get the root element node, else only return a
  // value XMLNode if the name of the root element matches \a name.
  //----------------------------------------------------------------------
  XMLNode GetRootElement(const char *required_name = nullptr);

  llvm::StringRef GetErrors() const;

  static void ErrorCallback(void *ctx, const char *format, ...);

  static bool XMLEnabled();

protected:
  XMLDocumentImpl m_document;
  StreamString m_errors;
};

class ApplePropertyList {
public:
  ApplePropertyList();

  ApplePropertyList(const char *path);

  ~ApplePropertyList();

  bool ParseFile(const char *path);

  llvm::StringRef GetErrors() const;

  explicit operator bool() const { return IsValid(); }

  bool IsValid() const;

  XMLNode GetValueNode(const char *key) const;

  bool GetValueAsString(const char *key, std::string &value) const;

  StructuredData::ObjectSP GetStructuredData();

protected:
  // Using a node returned from GetValueNode() extract its value as a string
  // (if possible). Array and dictionary nodes will return false as they have
  // no string value. Boolean nodes will return true and \a value will be
  // "true" or "false" as the string value comes from the element name itself.
  // All other nodes will return the text content of the XMLNode.
  static bool ExtractStringFromValueNode(const XMLNode &node,
                                         std::string &value);

  XMLDocument m_xml_doc;
  XMLNode m_dict_node;
};

} // namespace lldb_private

#endif // liblldb_XML_h_
