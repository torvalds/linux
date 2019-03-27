// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

#ifdef __i386__

// _chkstk (_alloca) routine - probe stack between %esp and (%esp-%eax) in 4k increments,
// then decrement %esp by %eax.  Preserves all registers except %esp and flags.
// This routine is windows specific
// http://msdn.microsoft.com/en-us/library/ms648426.aspx

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(_alloca) // _chkstk and _alloca are the same function
DEFINE_COMPILERRT_FUNCTION(__chkstk)
        push   %ecx
        cmp    $0x1000,%eax
        lea    8(%esp),%ecx     // esp before calling this routine -> ecx
        jb     1f
2:
        sub    $0x1000,%ecx
        test   %ecx,(%ecx)
        sub    $0x1000,%eax
        cmp    $0x1000,%eax
        ja     2b
1:
        sub    %eax,%ecx
        test   %ecx,(%ecx)

        lea    4(%esp),%eax     // load pointer to the return address into eax
        mov    %ecx,%esp        // install the new top of stack pointer into esp
        mov    -4(%eax),%ecx    // restore ecx
        push   (%eax)           // push return address onto the stack
        sub    %esp,%eax        // restore the original value in eax
        ret
END_COMPILERRT_FUNCTION(__chkstk)
END_COMPILERRT_FUNCTION(_alloca)

#endif // __i386__
