//===-- Mangled.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Mangled_h_
#define liblldb_Mangled_h_
#if defined(__cplusplus)

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include "lldb/Utility/ConstString.h"

#include "llvm/ADT/StringRef.h"

#include <memory>
#include <stddef.h>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Mangled Mangled.h "lldb/Core/Mangled.h"
/// A class that handles mangled names.
///
/// Designed to handle mangled names. The demangled version of any names will
/// be computed when the demangled name is accessed through the Demangled()
/// acccessor. This class can also tokenize the demangled version of the name
/// for powerful searches. Functions and symbols could make instances of this
/// class for their mangled names. Uniqued string pools are used for the
/// mangled, demangled, and token string values to allow for faster
/// comparisons and for efficient memory use.
//----------------------------------------------------------------------
class Mangled {
public:
  enum NamePreference {
    ePreferMangled,
    ePreferDemangled,
    ePreferDemangledWithoutArguments
  };

  enum ManglingScheme {
    eManglingSchemeNone = 0,
    eManglingSchemeMSVC,
    eManglingSchemeItanium
  };

  //----------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize with both mangled and demangled names empty.
  //----------------------------------------------------------------------
  Mangled();

  //----------------------------------------------------------------------
  /// Construct with name.
  ///
  /// Constructor with an optional string and a boolean indicating if it is
  /// the mangled version.
  ///
  /// @param[in] name
  ///     The already const name to copy into this object.
  ///
  /// @param[in] is_mangled
  ///     If \b true then \a name is a mangled name, if \b false then
  ///     \a name is demangled.
  //----------------------------------------------------------------------
  Mangled(const ConstString &name, bool is_mangled);
  Mangled(llvm::StringRef name, bool is_mangled);

  //----------------------------------------------------------------------
  /// Construct with name.
  ///
  /// Constructor with an optional string and auto-detect if \a name is
  /// mangled or not.
  ///
  /// @param[in] name
  ///     The already const name to copy into this object.
  //----------------------------------------------------------------------
  explicit Mangled(const ConstString &name);

  explicit Mangled(llvm::StringRef name);

  //----------------------------------------------------------------------
  /// Destructor
  ///
  /// Releases its ref counts on the mangled and demangled strings that live
  /// in the global string pool.
  //----------------------------------------------------------------------
  ~Mangled();

  //----------------------------------------------------------------------
  /// Convert to pointer operator.
  ///
  /// This allows code to check a Mangled object to see if it contains a valid
  /// mangled name using code such as:
  ///
  /// @code
  /// Mangled mangled(...);
  /// if (mangled)
  /// { ...
  /// @endcode
  ///
  /// @return
  ///     A pointer to this object if either the mangled or unmangled
  ///     name is set, NULL otherwise.
  //----------------------------------------------------------------------
  operator void *() const;

  //----------------------------------------------------------------------
  /// Logical NOT operator.
  ///
  /// This allows code to check a Mangled object to see if it contains an
  /// empty mangled name using code such as:
  ///
  /// @code
  /// Mangled mangled(...);
  /// if (!mangled)
  /// { ...
  /// @endcode
  ///
  /// @return
  ///     Returns \b true if the object has an empty mangled and
  ///     unmangled name, \b false otherwise.
  //----------------------------------------------------------------------
  bool operator!() const;

  //----------------------------------------------------------------------
  /// Clear the mangled and demangled values.
  //----------------------------------------------------------------------
  void Clear();

  //----------------------------------------------------------------------
  /// Compare the mangled string values
  ///
  /// Compares the Mangled::GetName() string in \a lhs and \a rhs.
  ///
  /// @param[in] lhs
  ///     A const reference to the Left Hand Side object to compare.
  ///
  /// @param[in] rhs
  ///     A const reference to the Right Hand Side object to compare.
  ///
  /// @return
  ///     @li -1 if \a lhs is less than \a rhs
  ///     @li 0 if \a lhs is equal to \a rhs
  ///     @li 1 if \a lhs is greater than \a rhs
  //----------------------------------------------------------------------
  static int Compare(const Mangled &lhs, const Mangled &rhs);

  //----------------------------------------------------------------------
  /// Dump a description of this object to a Stream \a s.
  ///
  /// Dump a Mangled object to stream \a s. We don't force our demangled name
  /// to be computed currently (we don't use the accessor).
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //----------------------------------------------------------------------
  void Dump(Stream *s) const;

  //----------------------------------------------------------------------
  /// Dump a debug description of this object to a Stream \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //----------------------------------------------------------------------
  void DumpDebug(Stream *s) const;

  //----------------------------------------------------------------------
  /// Demangled name get accessor.
  ///
  /// @return
  ///     A const reference to the demangled name string object.
  //----------------------------------------------------------------------
  const ConstString &GetDemangledName(lldb::LanguageType language) const;

  //----------------------------------------------------------------------
  /// Display demangled name get accessor.
  ///
  /// @return
  ///     A const reference to the display demangled name string object.
  //----------------------------------------------------------------------
  ConstString GetDisplayDemangledName(lldb::LanguageType language) const;

  void SetDemangledName(const ConstString &name) { m_demangled = name; }

  void SetMangledName(const ConstString &name) { m_mangled = name; }

  //----------------------------------------------------------------------
  /// Mangled name get accessor.
  ///
  /// @return
  ///     A reference to the mangled name string object.
  //----------------------------------------------------------------------
  ConstString &GetMangledName() { return m_mangled; }

  //----------------------------------------------------------------------
  /// Mangled name get accessor.
  ///
  /// @return
  ///     A const reference to the mangled name string object.
  //----------------------------------------------------------------------
  const ConstString &GetMangledName() const { return m_mangled; }

  //----------------------------------------------------------------------
  /// Best name get accessor.
  ///
  /// @param[in] preference
  ///     Which name would you prefer to get?
  ///
  /// @return
  ///     A const reference to the preferred name string object if this
  ///     object has a valid name of that kind, else a const reference to the
  ///     other name is returned.
  //----------------------------------------------------------------------
  ConstString GetName(lldb::LanguageType language,
                      NamePreference preference = ePreferDemangled) const;

  //----------------------------------------------------------------------
  /// Check if "name" matches either the mangled or demangled name.
  ///
  /// @param[in] name
  ///     A name to match against both strings.
  ///
  /// @return
  ///     \b True if \a name matches either name, \b false otherwise.
  //----------------------------------------------------------------------
  bool NameMatches(const ConstString &name, lldb::LanguageType language) const {
    if (m_mangled == name)
      return true;
    return GetDemangledName(language) == name;
  }
  bool NameMatches(const RegularExpression &regex,
                   lldb::LanguageType language) const;

  //----------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// Return the size in bytes that this object takes in memory. This returns
  /// the size in bytes of this object, not any shared string values it may
  /// refer to.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  ///
  /// @see ConstString::StaticMemorySize ()
  //----------------------------------------------------------------------
  size_t MemorySize() const;

  //----------------------------------------------------------------------
  /// Set the string value in this object.
  ///
  /// If \a is_mangled is \b true, then the mangled named is set to \a name,
  /// else the demangled name is set to \a name.
  ///
  /// @param[in] name
  ///     The already const version of the name for this object.
  ///
  /// @param[in] is_mangled
  ///     If \b true then \a name is a mangled name, if \b false then
  ///     \a name is demangled.
  //----------------------------------------------------------------------
  void SetValue(const ConstString &name, bool is_mangled);

  //----------------------------------------------------------------------
  /// Set the string value in this object.
  ///
  /// This version auto detects if the string is mangled by inspecting the
  /// string value and looking for common mangling prefixes.
  ///
  /// @param[in] name
  ///     The already const version of the name for this object.
  //----------------------------------------------------------------------
  void SetValue(const ConstString &name);

  //----------------------------------------------------------------------
  /// Try to guess the language from the mangling.
  ///
  /// For a mangled name to have a language it must have both a mangled and a
  /// demangled name and it can be guessed from the mangling what the language
  /// is.  Note: this will return C++ for any language that uses Itanium ABI
  /// mangling.
  ///
  /// Standard C function names will return eLanguageTypeUnknown because they
  /// aren't mangled and it isn't clear what language the name represents
  /// (there will be no mangled name).
  ///
  /// @return
  ///     The language for the mangled/demangled name, eLanguageTypeUnknown
  ///     if there is no mangled or demangled counterpart.
  //----------------------------------------------------------------------
  lldb::LanguageType GuessLanguage() const;

  /// Function signature for filtering mangled names.
  using SkipMangledNameFn = bool(llvm::StringRef, ManglingScheme);

  //----------------------------------------------------------------------
  /// Trigger explicit demangling to obtain rich mangling information. This is
  /// optimized for batch processing while populating a name index. To get the
  /// pure demangled name string for a single entity, use GetDemangledName()
  /// instead.
  ///
  /// For names that match the Itanium mangling scheme, this uses LLVM's
  /// ItaniumPartialDemangler. All other names fall back to LLDB's builtin
  /// parser currently.
  ///
  /// This function is thread-safe when used with different \a context
  /// instances in different threads.
  ///
  /// @param[in] context
  ///     The context for this function. A single instance can be stack-
  ///     allocated in the caller's frame and used for multiple calls.
  ///
  /// @param[in] skip_mangled_name
  ///     A filtering function for skipping entities based on name and mangling
  ///     scheme. This can be null if unused.
  ///
  /// @return
  ///     True on success, false otherwise.
  //----------------------------------------------------------------------
  bool DemangleWithRichManglingInfo(RichManglingContext &context,
                                    SkipMangledNameFn *skip_mangled_name);

private:
  //----------------------------------------------------------------------
  /// Mangled member variables.
  //----------------------------------------------------------------------
  ConstString m_mangled;           ///< The mangled version of the name
  mutable ConstString m_demangled; ///< Mutable so we can get it on demand with
                                   ///a const version of this object
};

Stream &operator<<(Stream &s, const Mangled &obj);

} // namespace lldb_private

#endif // #if defined(__cplusplus)
#endif // liblldb_Mangled_h_
