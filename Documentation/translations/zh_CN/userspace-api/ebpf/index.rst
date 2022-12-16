.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/userspace-api/ebpf/index.rst

:翻译:

 李睿 Rui Li <me@lirui.org>

eBPF 用户空间API
================

eBPF是一种在Linux内核中提供沙箱化运行环境的机制，它可以在不改变内核源码或加载
内核模块的情况下扩展运行时和编写工具。eBPF程序能够被附加到各种内核子系统中，包
括网络，跟踪和Linux安全模块(LSM)等。

关于eBPF的内部内核文档，请查看 Documentation/bpf/index.rst 。

.. toctree::
   :maxdepth: 1

   syscall
