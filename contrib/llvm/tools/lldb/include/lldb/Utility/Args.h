//===-- Args.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ARGS_H
#define LLDB_UTILITY_ARGS_H

#include "lldb/Utility/Environment.h"
#include "lldb/lldb-private-types.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>
#include <vector>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Args Args.h "lldb/Utility/Args.h"
/// A command line argument class.
///
/// The Args class is designed to be fed a command line. The command line is
/// copied into an internal buffer and then split up into arguments. Arguments
/// are space delimited if there are no quotes (single, double, or backtick
/// quotes) surrounding the argument. Spaces can be escaped using a \
/// character to avoid having to surround an argument that contains a space
/// with quotes.
//----------------------------------------------------------------------
class Args {
public:
  struct ArgEntry {
  private:
    friend class Args;
    std::unique_ptr<char[]> ptr;

    char *data() { return ptr.get(); }

  public:
    ArgEntry() = default;
    ArgEntry(llvm::StringRef str, char quote);

    llvm::StringRef ref;
    char quote;
    const char *c_str() const { return ptr.get(); }

    //------------------------------------------------------------------
    /// Returns true if this argument was quoted in any way.
    //------------------------------------------------------------------
    bool IsQuoted() const { return quote != '\0'; }
  };

  //------------------------------------------------------------------
  /// Construct with an option command string.
  ///
  /// @param[in] command
  ///     A NULL terminated command that will be copied and split up
  ///     into arguments.
  ///
  /// @see Args::SetCommandString(llvm::StringRef)
  //------------------------------------------------------------------
  Args(llvm::StringRef command = llvm::StringRef());

  Args(const Args &rhs);
  explicit Args(const StringList &list);

  Args &operator=(const Args &rhs);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~Args();

  explicit Args(const Environment &env) : Args() {
    SetArguments(const_cast<const char **>(env.getEnvp().get()));
  }

  explicit operator Environment() const { return GetConstArgumentVector(); }

  //------------------------------------------------------------------
  /// Dump all entries to the stream \a s using label \a label_name.
  ///
  /// If label_name is nullptr, the dump operation is skipped.
  ///
  /// @param[in] s
  ///     The stream to which to dump all arguments in the argument
  ///     vector.
  /// @param[in] label_name
  ///     The label_name to use as the label printed for each
  ///     entry of the args like so:
  ///       {label_name}[{index}]={value}
  //------------------------------------------------------------------
  void Dump(Stream &s, const char *label_name = "argv") const;

  //------------------------------------------------------------------
  /// Sets the command string contained by this object.
  ///
  /// The command string will be copied and split up into arguments that can
  /// be accessed via the accessor functions.
  ///
  /// @param[in] command
  ///     A command StringRef that will be copied and split up
  ///     into arguments.
  ///
  /// @see Args::GetArgumentCount() const
  /// @see Args::GetArgumentAtIndex (size_t) const @see
  /// Args::GetArgumentVector () @see Args::Shift () @see Args::Unshift (const
  /// char *)
  //------------------------------------------------------------------
  void SetCommandString(llvm::StringRef command);

  bool GetCommandString(std::string &command) const;

  bool GetQuotedCommandString(std::string &command) const;

  //------------------------------------------------------------------
  /// Gets the number of arguments left in this command object.
  ///
  /// @return
  ///     The number or arguments in this object.
  //------------------------------------------------------------------
  size_t GetArgumentCount() const;
  bool empty() const { return GetArgumentCount() == 0; }

  //------------------------------------------------------------------
  /// Gets the NULL terminated C string argument pointer for the argument at
  /// index \a idx.
  ///
  /// @return
  ///     The NULL terminated C string argument pointer if \a idx is a
  ///     valid argument index, NULL otherwise.
  //------------------------------------------------------------------
  const char *GetArgumentAtIndex(size_t idx) const;

  llvm::ArrayRef<ArgEntry> entries() const { return m_entries; }
  char GetArgumentQuoteCharAtIndex(size_t idx) const;

  using const_iterator = std::vector<ArgEntry>::const_iterator;

  const_iterator begin() const { return m_entries.begin(); }
  const_iterator end() const { return m_entries.end(); }

  size_t size() const { return GetArgumentCount(); }
  const ArgEntry &operator[](size_t n) const { return m_entries[n]; }

  //------------------------------------------------------------------
  /// Gets the argument vector.
  ///
  /// The value returned by this function can be used by any function that
  /// takes and vector. The return value is just like \a argv in the standard
  /// C entry point function:
  ///     \code
  ///         int main (int argc, const char **argv);
  ///     \endcode
  ///
  /// @return
  ///     An array of NULL terminated C string argument pointers that
  ///     also has a terminating NULL C string pointer
  //------------------------------------------------------------------
  char **GetArgumentVector();

  //------------------------------------------------------------------
  /// Gets the argument vector.
  ///
  /// The value returned by this function can be used by any function that
  /// takes and vector. The return value is just like \a argv in the standard
  /// C entry point function:
  ///     \code
  ///         int main (int argc, const char **argv);
  ///     \endcode
  ///
  /// @return
  ///     An array of NULL terminate C string argument pointers that
  ///     also has a terminating NULL C string pointer
  //------------------------------------------------------------------
  const char **GetConstArgumentVector() const;

  //------------------------------------------------------------------
  /// Gets the argument as an ArrayRef. Note that the return value does *not*
  /// have a nullptr const char * at the end, as the size of the list is
  /// embedded in the ArrayRef object.
  //------------------------------------------------------------------
  llvm::ArrayRef<const char *> GetArgumentArrayRef() const {
    return llvm::makeArrayRef(m_argv).drop_back();
  }

  //------------------------------------------------------------------
  /// Appends a new argument to the end of the list argument list.
  ///
  /// @param[in] arg_cstr
  ///     The new argument as a NULL terminated C string.
  ///
  /// @param[in] quote_char
  ///     If the argument was originally quoted, put in the quote char here.
  //------------------------------------------------------------------
  void AppendArgument(llvm::StringRef arg_str, char quote_char = '\0');

  void AppendArguments(const Args &rhs);

  void AppendArguments(const char **argv);

  //------------------------------------------------------------------
  /// Insert the argument value at index \a idx to \a arg_cstr.
  ///
  /// @param[in] idx
  ///     The index of where to insert the argument.
  ///
  /// @param[in] arg_cstr
  ///     The new argument as a NULL terminated C string.
  ///
  /// @param[in] quote_char
  ///     If the argument was originally quoted, put in the quote char here.
  ///
  /// @return
  ///     The NULL terminated C string of the copy of \a arg_cstr.
  //------------------------------------------------------------------
  void InsertArgumentAtIndex(size_t idx, llvm::StringRef arg_str,
                             char quote_char = '\0');

  //------------------------------------------------------------------
  /// Replaces the argument value at index \a idx to \a arg_cstr if \a idx is
  /// a valid argument index.
  ///
  /// @param[in] idx
  ///     The index of the argument that will have its value replaced.
  ///
  /// @param[in] arg_cstr
  ///     The new argument as a NULL terminated C string.
  ///
  /// @param[in] quote_char
  ///     If the argument was originally quoted, put in the quote char here.
  //------------------------------------------------------------------
  void ReplaceArgumentAtIndex(size_t idx, llvm::StringRef arg_str,
                              char quote_char = '\0');

  //------------------------------------------------------------------
  /// Deletes the argument value at index
  /// if \a idx is a valid argument index.
  ///
  /// @param[in] idx
  ///     The index of the argument that will have its value replaced.
  ///
  //------------------------------------------------------------------
  void DeleteArgumentAtIndex(size_t idx);

  //------------------------------------------------------------------
  /// Sets the argument vector value, optionally copying all arguments into an
  /// internal buffer.
  ///
  /// Sets the arguments to match those found in \a argv. All argument strings
  /// will be copied into an internal buffers.
  //
  //  FIXME: Handle the quote character somehow.
  //------------------------------------------------------------------
  void SetArguments(size_t argc, const char **argv);

  void SetArguments(const char **argv);

  //------------------------------------------------------------------
  /// Shifts the first argument C string value of the array off the argument
  /// array.
  ///
  /// The string value will be freed, so a copy of the string should be made
  /// by calling Args::GetArgumentAtIndex (size_t) const first and copying the
  /// returned value before calling Args::Shift().
  ///
  /// @see Args::GetArgumentAtIndex (size_t) const
  //------------------------------------------------------------------
  void Shift();

  //------------------------------------------------------------------
  /// Inserts a class owned copy of \a arg_cstr at the beginning of the
  /// argument vector.
  ///
  /// A copy \a arg_cstr will be made.
  ///
  /// @param[in] arg_cstr
  ///     The argument to push on the front of the argument stack.
  ///
  /// @param[in] quote_char
  ///     If the argument was originally quoted, put in the quote char here.
  //------------------------------------------------------------------
  void Unshift(llvm::StringRef arg_str, char quote_char = '\0');

  //------------------------------------------------------------------
  // Clear the arguments.
  //
  // For re-setting or blanking out the list of arguments.
  //------------------------------------------------------------------
  void Clear();

  static const char *StripSpaces(std::string &s, bool leading = true,
                                 bool trailing = true,
                                 bool return_null_if_empty = true);

  static bool UInt64ValueIsValidForByteSize(uint64_t uval64,
                                            size_t total_byte_size) {
    if (total_byte_size > 8)
      return false;

    if (total_byte_size == 8)
      return true;

    const uint64_t max = ((uint64_t)1 << (uint64_t)(total_byte_size * 8)) - 1;
    return uval64 <= max;
  }

  static bool SInt64ValueIsValidForByteSize(int64_t sval64,
                                            size_t total_byte_size) {
    if (total_byte_size > 8)
      return false;

    if (total_byte_size == 8)
      return true;

    const int64_t max = ((int64_t)1 << (uint64_t)(total_byte_size * 8 - 1)) - 1;
    const int64_t min = ~(max);
    return min <= sval64 && sval64 <= max;
  }

  static lldb::Encoding
  StringToEncoding(llvm::StringRef s,
                   lldb::Encoding fail_value = lldb::eEncodingInvalid);

  static uint32_t StringToGenericRegister(llvm::StringRef s);

  static const char *GetShellSafeArgument(const FileSpec &shell,
                                          const char *unsafe_arg,
                                          std::string &safe_arg);

  // EncodeEscapeSequences will change the textual representation of common
  // escape sequences like "\n" (two characters) into a single '\n'. It does
  // this for all of the supported escaped sequences and for the \0ooo (octal)
  // and \xXX (hex). The resulting "dst" string will contain the character
  // versions of all supported escape sequences. The common supported escape
  // sequences are: "\a", "\b", "\f", "\n", "\r", "\t", "\v", "\'", "\"", "\\".

  static void EncodeEscapeSequences(const char *src, std::string &dst);

  // ExpandEscapeSequences will change a string of possibly non-printable
  // characters and expand them into text. So '\n' will turn into two
  // characters like "\n" which is suitable for human reading. When a character
  // is not printable and isn't one of the common in escape sequences listed in
  // the help for EncodeEscapeSequences, then it will be encoded as octal.
  // Printable characters are left alone.
  static void ExpandEscapedCharacters(const char *src, std::string &dst);

  static std::string EscapeLLDBCommandArgument(const std::string &arg,
                                               char quote_char);

private:
  std::vector<ArgEntry> m_entries;
  std::vector<char *> m_argv;
};

//----------------------------------------------------------------------
/// @class OptionsWithRaw Args.h "lldb/Utility/Args.h"
/// A pair of an option list with a 'raw' string as a suffix.
///
/// This class works similar to Args, but handles the case where we have a
/// trailing string that shouldn't be interpreted as a list of arguments but
/// preserved as is. It is also only useful for handling command line options
/// (e.g. '-foo bar -i0') that start with a dash.
///
/// The leading option list is optional. If the first non-space character
/// in the string starts with a dash, and the string contains an argument
/// that is an unquoted double dash (' -- '), then everything up to the double
/// dash is parsed as a list of arguments. Everything after the double dash
/// is interpreted as the raw suffix string. Note that the space behind the
/// double dash is not part of the raw suffix.
///
/// All strings not matching the above format as considered to be just a raw
/// string without any options.
///
/// @see Args
//----------------------------------------------------------------------
class OptionsWithRaw {
public:
  //------------------------------------------------------------------
  /// Parse the given string as a list of optional arguments with a raw suffix.
  ///
  /// See the class description for a description of the input format.
  ///
  /// @param[in] argument_string
  ///     The string that should be parsed.
  //------------------------------------------------------------------
  explicit OptionsWithRaw(llvm::StringRef argument_string);

  //------------------------------------------------------------------
  /// Returns true if there are any arguments before the raw suffix.
  //------------------------------------------------------------------
  bool HasArgs() const { return m_has_args; }

  //------------------------------------------------------------------
  /// Returns the list of arguments.
  ///
  /// You can only call this method if HasArgs returns true.
  //------------------------------------------------------------------
  Args &GetArgs() {
    assert(m_has_args);
    return m_args;
  }

  //------------------------------------------------------------------
  /// Returns the list of arguments.
  ///
  /// You can only call this method if HasArgs returns true.
  //------------------------------------------------------------------
  const Args &GetArgs() const {
    assert(m_has_args);
    return m_args;
  }

  //------------------------------------------------------------------
  /// Returns the part of the input string that was used for parsing the
  /// argument list. This string also includes the double dash that is used
  /// for separating the argument list from the suffix.
  ///
  /// You can only call this method if HasArgs returns true.
  //------------------------------------------------------------------
  llvm::StringRef GetArgStringWithDelimiter() const {
    assert(m_has_args);
    return m_arg_string_with_delimiter;
  }

  //------------------------------------------------------------------
  /// Returns the part of the input string that was used for parsing the
  /// argument list.
  ///
  /// You can only call this method if HasArgs returns true.
  //------------------------------------------------------------------
  llvm::StringRef GetArgString() const {
    assert(m_has_args);
    return m_arg_string;
  }

  //------------------------------------------------------------------
  /// Returns the raw suffix part of the parsed string.
  //------------------------------------------------------------------
  const std::string &GetRawPart() const { return m_suffix; }

private:
  void SetFromString(llvm::StringRef arg_string);

  /// Keeps track if we have parsed and stored any arguments.
  bool m_has_args = false;
  Args m_args;
  llvm::StringRef m_arg_string;
  llvm::StringRef m_arg_string_with_delimiter;

  // FIXME: This should be a StringRef, but some of the calling code expect a
  // C string here so only a real std::string is possible.
  std::string m_suffix;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_ARGS_H
