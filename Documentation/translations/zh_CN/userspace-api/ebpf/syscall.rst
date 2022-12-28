.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/userspace-api/ebpf/syscall.rst

:翻译:

 李睿 Rui Li <me@lirui.org>

eBPF Syscall
------------

:作者:
    - Alexei Starovoitov <ast@kernel.org>
    - Joe Stringer <joe@wand.net.nz>
    - Michael Kerrisk <mtk.manpages@gmail.com>

bpf syscall的主要信息可以在 `man-pages`_ 中的 `bpf(2)`_ 找到。

bpf() 子命令参考
~~~~~~~~~~~~~~~~

子命令在以下内核代码中：

include/uapi/linux/bpf.h

.. Links:
.. _man-pages: https://www.kernel.org/doc/man-pages/
.. _bpf(2): https://man7.org/linux/man-pages/man2/bpf.2.html
