//===-- Args.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Args.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lldb;
using namespace lldb_private;

// A helper function for argument parsing.
// Parses the initial part of the first argument using normal double quote
// rules: backslash escapes the double quote and itself. The parsed string is
// appended to the second argument. The function returns the unparsed portion
// of the string, starting at the closing quote.
static llvm::StringRef ParseDoubleQuotes(llvm::StringRef quoted,
                                         std::string &result) {
  // Inside double quotes, '\' and '"' are special.
  static const char *k_escapable_characters = "\"\\";
  while (true) {
    // Skip over over regular characters and append them.
    size_t regular = quoted.find_first_of(k_escapable_characters);
    result += quoted.substr(0, regular);
    quoted = quoted.substr(regular);

    // If we have reached the end of string or the closing quote, we're done.
    if (quoted.empty() || quoted.front() == '"')
      break;

    // We have found a backslash.
    quoted = quoted.drop_front();

    if (quoted.empty()) {
      // A lone backslash at the end of string, let's just append it.
      result += '\\';
      break;
    }

    // If the character after the backslash is not a whitelisted escapable
    // character, we leave the character sequence untouched.
    if (strchr(k_escapable_characters, quoted.front()) == nullptr)
      result += '\\';

    result += quoted.front();
    quoted = quoted.drop_front();
  }

  return quoted;
}

static size_t ArgvToArgc(const char **argv) {
  if (!argv)
    return 0;
  size_t count = 0;
  while (*argv++)
    ++count;
  return count;
}

// Trims all whitespace that can separate command line arguments from the left
// side of the string.
static llvm::StringRef ltrimForArgs(llvm::StringRef str) {
  static const char *k_space_separators = " \t";
  return str.ltrim(k_space_separators);
}

// A helper function for SetCommandString. Parses a single argument from the
// command string, processing quotes and backslashes in a shell-like manner.
// The function returns a tuple consisting of the parsed argument, the quote
// char used, and the unparsed portion of the string starting at the first
// unqouted, unescaped whitespace character.
static std::tuple<std::string, char, llvm::StringRef>
ParseSingleArgument(llvm::StringRef command) {
  // Argument can be split into multiple discontiguous pieces, for example:
  //  "Hello ""World"
  // this would result in a single argument "Hello World" (without the quotes)
  // since the quotes would be removed and there is not space between the
  // strings.
  std::string arg;

  // Since we can have multiple quotes that form a single command in a command
  // like: "Hello "world'!' (which will make a single argument "Hello world!")
  // we remember the first quote character we encounter and use that for the
  // quote character.
  char first_quote_char = '\0';

  bool arg_complete = false;
  do {
    // Skip over over regular characters and append them.
    size_t regular = command.find_first_of(" \t\"'`\\");
    arg += command.substr(0, regular);
    command = command.substr(regular);

    if (command.empty())
      break;

    char special = command.front();
    command = command.drop_front();
    switch (special) {
    case '\\':
      if (command.empty()) {
        arg += '\\';
        break;
      }

      // If the character after the backslash is not a whitelisted escapable
      // character, we leave the character sequence untouched.
      if (strchr(" \t\\'\"`", command.front()) == nullptr)
        arg += '\\';

      arg += command.front();
      command = command.drop_front();

      break;

    case ' ':
    case '\t':
      // We are not inside any quotes, we just found a space after an argument.
      // We are done.
      arg_complete = true;
      break;

    case '"':
    case '\'':
    case '`':
      // We found the start of a quote scope.
      if (first_quote_char == '\0')
        first_quote_char = special;

      if (special == '"')
        command = ParseDoubleQuotes(command, arg);
      else {
        // For single quotes, we simply skip ahead to the matching quote
        // character (or the end of the string).
        size_t quoted = command.find(special);
        arg += command.substr(0, quoted);
        command = command.substr(quoted);
      }

      // If we found a closing quote, skip it.
      if (!command.empty())
        command = command.drop_front();

      break;
    }
  } while (!arg_complete);

  return std::make_tuple(arg, first_quote_char, command);
}

Args::ArgEntry::ArgEntry(llvm::StringRef str, char quote) : quote(quote) {
  size_t size = str.size();
  ptr.reset(new char[size + 1]);

  ::memcpy(data(), str.data() ? str.data() : "", size);
  ptr[size] = 0;
  ref = llvm::StringRef(c_str(), size);
}

//----------------------------------------------------------------------
// Args constructor
//----------------------------------------------------------------------
Args::Args(llvm::StringRef command) { SetCommandString(command); }

Args::Args(const Args &rhs) { *this = rhs; }

Args::Args(const StringList &list) : Args() {
  for (size_t i = 0; i < list.GetSize(); ++i)
    AppendArgument(list[i]);
}

Args &Args::operator=(const Args &rhs) {
  Clear();

  m_argv.clear();
  m_entries.clear();
  for (auto &entry : rhs.m_entries) {
    m_entries.emplace_back(entry.ref, entry.quote);
    m_argv.push_back(m_entries.back().data());
  }
  m_argv.push_back(nullptr);
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
Args::~Args() {}

void Args::Dump(Stream &s, const char *label_name) const {
  if (!label_name)
    return;

  int i = 0;
  for (auto &entry : m_entries) {
    s.Indent();
    s.Format("{0}[{1}]=\"{2}\"\n", label_name, i++, entry.ref);
  }
  s.Format("{0}[{1}]=NULL\n", label_name, i);
  s.EOL();
}

bool Args::GetCommandString(std::string &command) const {
  command.clear();

  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (i > 0)
      command += ' ';
    command += m_entries[i].ref;
  }

  return !m_entries.empty();
}

bool Args::GetQuotedCommandString(std::string &command) const {
  command.clear();

  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (i > 0)
      command += ' ';

    if (m_entries[i].quote) {
      command += m_entries[i].quote;
      command += m_entries[i].ref;
      command += m_entries[i].quote;
    } else {
      command += m_entries[i].ref;
    }
  }

  return !m_entries.empty();
}

void Args::SetCommandString(llvm::StringRef command) {
  Clear();
  m_argv.clear();

  command = ltrimForArgs(command);
  std::string arg;
  char quote;
  while (!command.empty()) {
    std::tie(arg, quote, command) = ParseSingleArgument(command);
    m_entries.emplace_back(arg, quote);
    m_argv.push_back(m_entries.back().data());
    command = ltrimForArgs(command);
  }
  m_argv.push_back(nullptr);
}

size_t Args::GetArgumentCount() const { return m_entries.size(); }

const char *Args::GetArgumentAtIndex(size_t idx) const {
  if (idx < m_argv.size())
    return m_argv[idx];
  return nullptr;
}

char Args::GetArgumentQuoteCharAtIndex(size_t idx) const {
  if (idx < m_entries.size())
    return m_entries[idx].quote;
  return '\0';
}

char **Args::GetArgumentVector() {
  assert(!m_argv.empty());
  // TODO: functions like execve and posix_spawnp exhibit undefined behavior
  // when argv or envp is null.  So the code below is actually wrong.  However,
  // other code in LLDB depends on it being null.  The code has been acting
  // this way for some time, so it makes sense to leave it this way until
  // someone has the time to come along and fix it.
  return (m_argv.size() > 1) ? m_argv.data() : nullptr;
}

const char **Args::GetConstArgumentVector() const {
  assert(!m_argv.empty());
  return (m_argv.size() > 1) ? const_cast<const char **>(m_argv.data())
                             : nullptr;
}

void Args::Shift() {
  // Don't pop the last NULL terminator from the argv array
  if (m_entries.empty())
    return;
  m_argv.erase(m_argv.begin());
  m_entries.erase(m_entries.begin());
}

void Args::Unshift(llvm::StringRef arg_str, char quote_char) {
  InsertArgumentAtIndex(0, arg_str, quote_char);
}

void Args::AppendArguments(const Args &rhs) {
  assert(m_argv.size() == m_entries.size() + 1);
  assert(m_argv.back() == nullptr);
  m_argv.pop_back();
  for (auto &entry : rhs.m_entries) {
    m_entries.emplace_back(entry.ref, entry.quote);
    m_argv.push_back(m_entries.back().data());
  }
  m_argv.push_back(nullptr);
}

void Args::AppendArguments(const char **argv) {
  size_t argc = ArgvToArgc(argv);

  assert(m_argv.size() == m_entries.size() + 1);
  assert(m_argv.back() == nullptr);
  m_argv.pop_back();
  for (auto arg : llvm::makeArrayRef(argv, argc)) {
    m_entries.emplace_back(arg, '\0');
    m_argv.push_back(m_entries.back().data());
  }

  m_argv.push_back(nullptr);
}

void Args::AppendArgument(llvm::StringRef arg_str, char quote_char) {
  InsertArgumentAtIndex(GetArgumentCount(), arg_str, quote_char);
}

void Args::InsertArgumentAtIndex(size_t idx, llvm::StringRef arg_str,
                                 char quote_char) {
  assert(m_argv.size() == m_entries.size() + 1);
  assert(m_argv.back() == nullptr);

  if (idx > m_entries.size())
    return;
  m_entries.emplace(m_entries.begin() + idx, arg_str, quote_char);
  m_argv.insert(m_argv.begin() + idx, m_entries[idx].data());
}

void Args::ReplaceArgumentAtIndex(size_t idx, llvm::StringRef arg_str,
                                  char quote_char) {
  assert(m_argv.size() == m_entries.size() + 1);
  assert(m_argv.back() == nullptr);

  if (idx >= m_entries.size())
    return;

  if (arg_str.size() > m_entries[idx].ref.size()) {
    m_entries[idx] = ArgEntry(arg_str, quote_char);
    m_argv[idx] = m_entries[idx].data();
  } else {
    const char *src_data = arg_str.data() ? arg_str.data() : "";
    ::memcpy(m_entries[idx].data(), src_data, arg_str.size());
    m_entries[idx].ptr[arg_str.size()] = 0;
    m_entries[idx].ref = m_entries[idx].ref.take_front(arg_str.size());
  }
}

void Args::DeleteArgumentAtIndex(size_t idx) {
  if (idx >= m_entries.size())
    return;

  m_argv.erase(m_argv.begin() + idx);
  m_entries.erase(m_entries.begin() + idx);
}

void Args::SetArguments(size_t argc, const char **argv) {
  Clear();

  auto args = llvm::makeArrayRef(argv, argc);
  m_entries.resize(argc);
  m_argv.resize(argc + 1);
  for (size_t i = 0; i < args.size(); ++i) {
    char quote =
        ((args[i][0] == '\'') || (args[i][0] == '"') || (args[i][0] == '`'))
            ? args[i][0]
            : '\0';

    m_entries[i] = ArgEntry(args[i], quote);
    m_argv[i] = m_entries[i].data();
  }
}

void Args::SetArguments(const char **argv) {
  SetArguments(ArgvToArgc(argv), argv);
}

void Args::Clear() {
  m_entries.clear();
  m_argv.clear();
  m_argv.push_back(nullptr);
}

const char *Args::StripSpaces(std::string &s, bool leading, bool trailing,
                              bool return_null_if_empty) {
  static const char *k_white_space = " \t\v";
  if (!s.empty()) {
    if (leading) {
      size_t pos = s.find_first_not_of(k_white_space);
      if (pos == std::string::npos)
        s.clear();
      else if (pos > 0)
        s.erase(0, pos);
    }

    if (trailing) {
      size_t rpos = s.find_last_not_of(k_white_space);
      if (rpos != std::string::npos && rpos + 1 < s.size())
        s.erase(rpos + 1);
    }
  }
  if (return_null_if_empty && s.empty())
    return nullptr;
  return s.c_str();
}

const char *Args::GetShellSafeArgument(const FileSpec &shell,
                                       const char *unsafe_arg,
                                       std::string &safe_arg) {
  struct ShellDescriptor {
    ConstString m_basename;
    const char *m_escapables;
  };

  static ShellDescriptor g_Shells[] = {{ConstString("bash"), " '\"<>()&"},
                                       {ConstString("tcsh"), " '\"<>()&$"},
                                       {ConstString("sh"), " '\"<>()&"}};

  // safe minimal set
  const char *escapables = " '\"";

  if (auto basename = shell.GetFilename()) {
    for (const auto &Shell : g_Shells) {
      if (Shell.m_basename == basename) {
        escapables = Shell.m_escapables;
        break;
      }
    }
  }

  safe_arg.assign(unsafe_arg);
  size_t prev_pos = 0;
  while (prev_pos < safe_arg.size()) {
    // Escape spaces and quotes
    size_t pos = safe_arg.find_first_of(escapables, prev_pos);
    if (pos != std::string::npos) {
      safe_arg.insert(pos, 1, '\\');
      prev_pos = pos + 2;
    } else
      break;
  }
  return safe_arg.c_str();
}

lldb::Encoding Args::StringToEncoding(llvm::StringRef s,
                                      lldb::Encoding fail_value) {
  return llvm::StringSwitch<lldb::Encoding>(s)
      .Case("uint", eEncodingUint)
      .Case("sint", eEncodingSint)
      .Case("ieee754", eEncodingIEEE754)
      .Case("vector", eEncodingVector)
      .Default(fail_value);
}

uint32_t Args::StringToGenericRegister(llvm::StringRef s) {
  if (s.empty())
    return LLDB_INVALID_REGNUM;
  uint32_t result = llvm::StringSwitch<uint32_t>(s)
                        .Case("pc", LLDB_REGNUM_GENERIC_PC)
                        .Case("sp", LLDB_REGNUM_GENERIC_SP)
                        .Case("fp", LLDB_REGNUM_GENERIC_FP)
                        .Cases("ra", "lr", LLDB_REGNUM_GENERIC_RA)
                        .Case("flags", LLDB_REGNUM_GENERIC_FLAGS)
                        .Case("arg1", LLDB_REGNUM_GENERIC_ARG1)
                        .Case("arg2", LLDB_REGNUM_GENERIC_ARG2)
                        .Case("arg3", LLDB_REGNUM_GENERIC_ARG3)
                        .Case("arg4", LLDB_REGNUM_GENERIC_ARG4)
                        .Case("arg5", LLDB_REGNUM_GENERIC_ARG5)
                        .Case("arg6", LLDB_REGNUM_GENERIC_ARG6)
                        .Case("arg7", LLDB_REGNUM_GENERIC_ARG7)
                        .Case("arg8", LLDB_REGNUM_GENERIC_ARG8)
                        .Default(LLDB_INVALID_REGNUM);
  return result;
}

void Args::EncodeEscapeSequences(const char *src, std::string &dst) {
  dst.clear();
  if (src) {
    for (const char *p = src; *p != '\0'; ++p) {
      size_t non_special_chars = ::strcspn(p, "\\");
      if (non_special_chars > 0) {
        dst.append(p, non_special_chars);
        p += non_special_chars;
        if (*p == '\0')
          break;
      }

      if (*p == '\\') {
        ++p; // skip the slash
        switch (*p) {
        case 'a':
          dst.append(1, '\a');
          break;
        case 'b':
          dst.append(1, '\b');
          break;
        case 'f':
          dst.append(1, '\f');
          break;
        case 'n':
          dst.append(1, '\n');
          break;
        case 'r':
          dst.append(1, '\r');
          break;
        case 't':
          dst.append(1, '\t');
          break;
        case 'v':
          dst.append(1, '\v');
          break;
        case '\\':
          dst.append(1, '\\');
          break;
        case '\'':
          dst.append(1, '\'');
          break;
        case '"':
          dst.append(1, '"');
          break;
        case '0':
          // 1 to 3 octal chars
          {
            // Make a string that can hold onto the initial zero char, up to 3
            // octal digits, and a terminating NULL.
            char oct_str[5] = {'\0', '\0', '\0', '\0', '\0'};

            int i;
            for (i = 0; (p[i] >= '0' && p[i] <= '7') && i < 4; ++i)
              oct_str[i] = p[i];

            // We don't want to consume the last octal character since the main
            // for loop will do this for us, so we advance p by one less than i
            // (even if i is zero)
            p += i - 1;
            unsigned long octal_value = ::strtoul(oct_str, nullptr, 8);
            if (octal_value <= UINT8_MAX) {
              dst.append(1, (char)octal_value);
            }
          }
          break;

        case 'x':
          // hex number in the format
          if (isxdigit(p[1])) {
            ++p; // Skip the 'x'

            // Make a string that can hold onto two hex chars plus a
            // NULL terminator
            char hex_str[3] = {*p, '\0', '\0'};
            if (isxdigit(p[1])) {
              ++p; // Skip the first of the two hex chars
              hex_str[1] = *p;
            }

            unsigned long hex_value = strtoul(hex_str, nullptr, 16);
            if (hex_value <= UINT8_MAX)
              dst.append(1, (char)hex_value);
          } else {
            dst.append(1, 'x');
          }
          break;

        default:
          // Just desensitize any other character by just printing what came
          // after the '\'
          dst.append(1, *p);
          break;
        }
      }
    }
  }
}

void Args::ExpandEscapedCharacters(const char *src, std::string &dst) {
  dst.clear();
  if (src) {
    for (const char *p = src; *p != '\0'; ++p) {
      if (isprint(*p))
        dst.append(1, *p);
      else {
        switch (*p) {
        case '\a':
          dst.append("\\a");
          break;
        case '\b':
          dst.append("\\b");
          break;
        case '\f':
          dst.append("\\f");
          break;
        case '\n':
          dst.append("\\n");
          break;
        case '\r':
          dst.append("\\r");
          break;
        case '\t':
          dst.append("\\t");
          break;
        case '\v':
          dst.append("\\v");
          break;
        case '\'':
          dst.append("\\'");
          break;
        case '"':
          dst.append("\\\"");
          break;
        case '\\':
          dst.append("\\\\");
          break;
        default: {
          // Just encode as octal
          dst.append("\\0");
          char octal_str[32];
          snprintf(octal_str, sizeof(octal_str), "%o", *p);
          dst.append(octal_str);
        } break;
        }
      }
    }
  }
}

std::string Args::EscapeLLDBCommandArgument(const std::string &arg,
                                            char quote_char) {
  const char *chars_to_escape = nullptr;
  switch (quote_char) {
  case '\0':
    chars_to_escape = " \t\\'\"`";
    break;
  case '\'':
    chars_to_escape = "";
    break;
  case '"':
    chars_to_escape = "$\"`\\";
    break;
  default:
    assert(false && "Unhandled quote character");
  }

  std::string res;
  res.reserve(arg.size());
  for (char c : arg) {
    if (::strchr(chars_to_escape, c))
      res.push_back('\\');
    res.push_back(c);
  }
  return res;
}

OptionsWithRaw::OptionsWithRaw(llvm::StringRef arg_string) {
  SetFromString(arg_string);
}

void OptionsWithRaw::SetFromString(llvm::StringRef arg_string) {
  const llvm::StringRef original_args = arg_string;

  arg_string = ltrimForArgs(arg_string);
  std::string arg;
  char quote;

  // If the string doesn't start with a dash, we just have no options and just
  // a raw part.
  if (!arg_string.startswith("-")) {
    m_suffix = original_args;
    return;
  }

  bool found_suffix = false;

  while (!arg_string.empty()) {
    // The length of the prefix before parsing.
    std::size_t prev_prefix_length = original_args.size() - arg_string.size();

    // Parse the next argument from the remaining string.
    std::tie(arg, quote, arg_string) = ParseSingleArgument(arg_string);

    // If we get an unquoted '--' argument, then we reached the suffix part
    // of the command.
    Args::ArgEntry entry(arg, quote);
    if (!entry.IsQuoted() && arg == "--") {
      // The remaining line is the raw suffix, and the line we parsed so far
      // needs to be interpreted as arguments.
      m_has_args = true;
      m_suffix = arg_string;
      found_suffix = true;

      // The length of the prefix after parsing.
      std::size_t prefix_length = original_args.size() - arg_string.size();

      // Take the string we know contains all the arguments and actually parse
      // it as proper arguments.
      llvm::StringRef prefix = original_args.take_front(prev_prefix_length);
      m_args = Args(prefix);
      m_arg_string = prefix;

      // We also record the part of the string that contains the arguments plus
      // the delimiter.
      m_arg_string_with_delimiter = original_args.take_front(prefix_length);

      // As the rest of the string became the raw suffix, we are done here.
      break;
    }

    arg_string = ltrimForArgs(arg_string);
  }

  // If we didn't find a suffix delimiter, the whole string is the raw suffix.
  if (!found_suffix) {
    found_suffix = true;
    m_suffix = original_args;
  }
}
