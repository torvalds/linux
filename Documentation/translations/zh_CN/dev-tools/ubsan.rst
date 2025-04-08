.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/ubsan.rst

:翻译:

 慕冬亮 Dongliang Mu <dzm91@hust.edu.cn>

:校译:

 王昱力 WangYuli <wangyuli@uniontech.com>

未定义行为消毒剂 - UBSAN
====================================

UBSAN是一种动态未定义行为检查工具。

UBSAN使用编译时插桩捕捉未定义行为。编译器在可能导致未定义行为的操作前插入特定
检测代码。如果检查失败，即检测到未定义行为，__ubsan_handle_* 函数将被调用打印
错误信息。

GCC自4.9.x [1_] （详见 ``-fsanitize=undefined`` 选项及其子选项）版本后引入这
一特性。GCC 5.x 版本实现了更多检查器 [2_]。

报告样例
--------------

::

	 ================================================================================
	 UBSAN: Undefined behaviour in ../include/linux/bitops.h:110:33
	 shift exponent 32 is to large for 32-bit type 'unsigned int'
	 CPU: 0 PID: 0 Comm: swapper Not tainted 4.4.0-rc1+ #26
	  0000000000000000 ffffffff82403cc8 ffffffff815e6cd6 0000000000000001
	  ffffffff82403cf8 ffffffff82403ce0 ffffffff8163a5ed 0000000000000020
	  ffffffff82403d78 ffffffff8163ac2b ffffffff815f0001 0000000000000002
	 Call Trace:
	  [<ffffffff815e6cd6>] dump_stack+0x45/0x5f
	  [<ffffffff8163a5ed>] ubsan_epilogue+0xd/0x40
	  [<ffffffff8163ac2b>] __ubsan_handle_shift_out_of_bounds+0xeb/0x130
	  [<ffffffff815f0001>] ? radix_tree_gang_lookup_slot+0x51/0x150
	  [<ffffffff8173c586>] _mix_pool_bytes+0x1e6/0x480
	  [<ffffffff83105653>] ? dmi_walk_early+0x48/0x5c
	  [<ffffffff8173c881>] add_device_randomness+0x61/0x130
	  [<ffffffff83105b35>] ? dmi_save_one_device+0xaa/0xaa
	  [<ffffffff83105653>] dmi_walk_early+0x48/0x5c
	  [<ffffffff831066ae>] dmi_scan_machine+0x278/0x4b4
	  [<ffffffff8111d58a>] ? vprintk_default+0x1a/0x20
	  [<ffffffff830ad120>] ? early_idt_handler_array+0x120/0x120
	  [<ffffffff830b2240>] setup_arch+0x405/0xc2c
	  [<ffffffff830ad120>] ? early_idt_handler_array+0x120/0x120
	  [<ffffffff830ae053>] start_kernel+0x83/0x49a
	  [<ffffffff830ad120>] ? early_idt_handler_array+0x120/0x120
	  [<ffffffff830ad386>] x86_64_start_reservations+0x2a/0x2c
	  [<ffffffff830ad4f3>] x86_64_start_kernel+0x16b/0x17a
	 ================================================================================

用法
-----

使用如下内核配置启用UBSAN::

  CONFIG_UBSAN=y

排除要被检测的文件::

  UBSAN_SANITIZE_main.o := n

排除一个目录中的所有文件::

  UBSAN_SANITIZE := n

当全部文件都被禁用，可通过如下方式为特定文件启用::

  UBSAN_SANITIZE_main.o := y

未对齐的内存访问检测可通过开启独立选项 - CONFIG_UBSAN_ALIGNMENT 检测。
该选项在支持未对齐访问的架构上(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS=y)
默认为关闭。该选项仍可通过内核配置启用，但它将产生大量的UBSAN报告。

参考文献
----------

.. _1: https://gcc.gnu.org/onlinedocs/gcc-4.9.0/gcc/Debugging-Options.html
.. _2: https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html
.. _3: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
