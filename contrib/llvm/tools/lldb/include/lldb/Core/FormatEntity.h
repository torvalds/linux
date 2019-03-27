//===-- FormatEntity.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FormatEntity_h_
#define liblldb_FormatEntity_h_

#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"
#include <algorithm>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace lldb_private {
class Address;
}
namespace lldb_private {
class ExecutionContext;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class StringList;
}
namespace lldb_private {
class SymbolContext;
}
namespace lldb_private {
class ValueObject;
}
namespace llvm {
class StringRef;
}

namespace lldb_private {
class FormatEntity {
public:
  struct Entry {
    enum class Type {
      Invalid,
      ParentNumber,
      ParentString,
      InsertString,
      Root,
      String,
      Scope,
      Variable,
      VariableSynthetic,
      ScriptVariable,
      ScriptVariableSynthetic,
      AddressLoad,
      AddressFile,
      AddressLoadOrFile,
      ProcessID,
      ProcessFile,
      ScriptProcess,
      ThreadID,
      ThreadProtocolID,
      ThreadIndexID,
      ThreadName,
      ThreadQueue,
      ThreadStopReason,
      ThreadReturnValue,
      ThreadCompletedExpression,
      ScriptThread,
      ThreadInfo,
      TargetArch,
      ScriptTarget,
      ModuleFile,
      File,
      Lang,
      FrameIndex,
      FrameNoDebug,
      FrameRegisterPC,
      FrameRegisterSP,
      FrameRegisterFP,
      FrameRegisterFlags,
      FrameRegisterByName,
      FrameIsArtificial,
      ScriptFrame,
      FunctionID,
      FunctionDidChange,
      FunctionInitialFunction,
      FunctionName,
      FunctionNameWithArgs,
      FunctionNameNoArgs,
      FunctionAddrOffset,
      FunctionAddrOffsetConcrete,
      FunctionLineOffset,
      FunctionPCOffset,
      FunctionInitial,
      FunctionChanged,
      FunctionIsOptimized,
      LineEntryFile,
      LineEntryLineNumber,
      LineEntryColumn,
      LineEntryStartAddress,
      LineEntryEndAddress,
      CurrentPCArrow
    };

    enum FormatType { None, UInt32, UInt64, CString };

    struct Definition {
      const char *name;
      const char *string; // Insert this exact string into the output
      Entry::Type type;
      FormatType format_type; // uint32_t, uint64_t, cstr, or anything that can
                              // be formatted by printf or lldb::Format
      uint64_t data;
      uint32_t num_children;
      Definition *children; // An array of "num_children" Definition entries,
      bool keep_separator;
    };

    Entry(Type t = Type::Invalid, const char *s = nullptr,
          const char *f = nullptr)
        : string(s ? s : ""), printf_format(f ? f : ""), children(),
          definition(nullptr), type(t), fmt(lldb::eFormatDefault), number(0),
          deref(false) {}

    Entry(llvm::StringRef s);
    Entry(char ch);

    void AppendChar(char ch);

    void AppendText(const llvm::StringRef &s);

    void AppendText(const char *cstr);

    void AppendEntry(const Entry &&entry) { children.push_back(entry); }

    void Clear() {
      string.clear();
      printf_format.clear();
      children.clear();
      definition = nullptr;
      type = Type::Invalid;
      fmt = lldb::eFormatDefault;
      number = 0;
      deref = false;
    }

    static const char *TypeToCString(Type t);

    void Dump(Stream &s, int depth = 0) const;

    bool operator==(const Entry &rhs) const {
      if (string != rhs.string)
        return false;
      if (printf_format != rhs.printf_format)
        return false;
      const size_t n = children.size();
      const size_t m = rhs.children.size();
      for (size_t i = 0; i < std::min<size_t>(n, m); ++i) {
        if (!(children[i] == rhs.children[i]))
          return false;
      }
      if (children != rhs.children)
        return false;
      if (definition != rhs.definition)
        return false;
      if (type != rhs.type)
        return false;
      if (fmt != rhs.fmt)
        return false;
      if (deref != rhs.deref)
        return false;
      return true;
    }

    std::string string;
    std::string printf_format;
    std::vector<Entry> children;
    Definition *definition;
    Type type;
    lldb::Format fmt;
    lldb::addr_t number;
    bool deref;
  };

  static bool Format(const Entry &entry, Stream &s, const SymbolContext *sc,
                     const ExecutionContext *exe_ctx, const Address *addr,
                     ValueObject *valobj, bool function_changed,
                     bool initial_function);

  static bool FormatStringRef(const llvm::StringRef &format, Stream &s,
                              const SymbolContext *sc,
                              const ExecutionContext *exe_ctx,
                              const Address *addr, ValueObject *valobj,
                              bool function_changed, bool initial_function);

  static bool FormatCString(const char *format, Stream &s,
                            const SymbolContext *sc,
                            const ExecutionContext *exe_ctx,
                            const Address *addr, ValueObject *valobj,
                            bool function_changed, bool initial_function);

  static Status Parse(const llvm::StringRef &format, Entry &entry);

  static Status ExtractVariableInfo(llvm::StringRef &format_str,
                                    llvm::StringRef &variable_name,
                                    llvm::StringRef &variable_format);

  static size_t AutoComplete(lldb_private::CompletionRequest &request);

  //----------------------------------------------------------------------
  // Format the current elements into the stream \a s.
  //
  // The root element will be stripped off and the format str passed in will be
  // either an empty string (print a description of this object), or contain a
  // `.`-separated series like a domain name that identifies further
  //  sub-elements to display.
  //----------------------------------------------------------------------
  static bool FormatFileSpec(const FileSpec &file, Stream &s,
                             llvm::StringRef elements,
                             llvm::StringRef element_format);

protected:
  static Status ParseInternal(llvm::StringRef &format, Entry &parent_entry,
                              uint32_t depth);
};
} // namespace lldb_private

#endif // liblldb_FormatEntity_h_
