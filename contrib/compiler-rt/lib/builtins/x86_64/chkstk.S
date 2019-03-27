// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// _chkstk routine
// This routine is windows specific
// http://msdn.microsoft.com/en-us/library/ms648426.aspx

// Notes from r227519
// MSVC x64s __chkstk and cygmings ___chkstk_ms do not adjust %rsp
// themselves. It also does not clobber %rax so we can reuse it when
// adjusting %rsp.

#ifdef __x86_64__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(___chkstk_ms)
        push   %rcx
        push   %rax
        cmp    $0x1000,%rax
        lea    24(%rsp),%rcx
        jb     1f
2:
        sub    $0x1000,%rcx
        test   %rcx,(%rcx)
        sub    $0x1000,%rax
        cmp    $0x1000,%rax
        ja     2b
1:
        sub    %rax,%rcx
        test   %rcx,(%rcx)
        pop    %rax
        pop    %rcx
        ret
END_COMPILERRT_FUNCTION(___chkstk_ms)

#endif // __x86_64__
