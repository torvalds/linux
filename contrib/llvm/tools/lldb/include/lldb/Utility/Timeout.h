//===-- Timeout.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Timeout_h_
#define liblldb_Timeout_h_

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/FormatProviders.h"

namespace lldb_private {

// A general purpose class for representing timeouts for various APIs. It's
// basically an llvm::Optional<std::chrono::duration<int64_t, Ratio>>, but we
// customize it a bit to enable the standard chrono implicit conversions (e.g.
// from Timeout<std::milli> to Timeout<std::micro>.
//
// The intended meaning of the values is:
// - llvm::None - no timeout, the call should wait forever - 0 - poll, only
// complete the call if it will not block - >0 - wait for a given number of
// units for the result
template <typename Ratio>
class Timeout : public llvm::Optional<std::chrono::duration<int64_t, Ratio>> {
private:
  template <typename Ratio2> using Dur = std::chrono::duration<int64_t, Ratio2>;
  template <typename Rep2, typename Ratio2>
  using EnableIf = std::enable_if<
      std::is_convertible<std::chrono::duration<Rep2, Ratio2>,
                          std::chrono::duration<int64_t, Ratio>>::value>;

  using Base = llvm::Optional<Dur<Ratio>>;

public:
  Timeout(llvm::NoneType none) : Base(none) {}
  Timeout(const Timeout &other) = default;

  template <typename Ratio2,
            typename = typename EnableIf<int64_t, Ratio2>::type>
  Timeout(const Timeout<Ratio2> &other)
      : Base(other ? Base(Dur<Ratio>(*other)) : llvm::None) {}

  template <typename Rep2, typename Ratio2,
            typename = typename EnableIf<Rep2, Ratio2>::type>
  Timeout(const std::chrono::duration<Rep2, Ratio2> &other)
      : Base(Dur<Ratio>(other)) {}
};

} // namespace lldb_private

namespace llvm {
template<typename Ratio>
struct format_provider<lldb_private::Timeout<Ratio>, void> {
  static void format(const lldb_private::Timeout<Ratio> &timeout,
                     raw_ostream &OS, StringRef Options) {
    typedef typename lldb_private::Timeout<Ratio>::value_type Dur;

    if (!timeout)
      OS << "<infinite>";
    else
      format_provider<Dur>::format(*timeout, OS, Options);
  }
};
}

#endif // liblldb_Timeout_h_
