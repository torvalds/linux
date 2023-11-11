.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../admin-guide/tainted-kernels`

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

受污染的内核
-------------

当发生一些在稍后调查问题时可能相关的事件时，内核会将自己标记为“受污染
（tainted）”的。不用太过担心，大多数情况下运行受污染的内核没有问题；这些信息
主要在有人想调查某个问题时才有意义的，因为问题的真正原因可能是导致内核受污染
的事件。这就是为什么来自受污染内核的缺陷报告常常被开发人员忽略，因此请尝试用
未受污染的内核重现问题。

请注意，即使在您消除导致污染的原因（亦即卸载专有内核模块）之后，内核仍将保持
污染状态，以表示内核仍然不可信。这也是为什么内核在注意到内部问题（“kernel
bug”）、可恢复错误（“kernel oops”）或不可恢复错误（“kernel panic”）时会打印
受污染状态，并将有关此的调试信息写入日志 ``dmesg`` 输出。也可以通过
``/proc/`` 中的文件在运行时检查受污染的状态。


BUG、Oops或Panics消息中的污染标志
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

在顶部以“CPU:”开头的一行中可以找到受污染的状态；内核是否受到污染和原因会显示
在进程ID（“PID:”）和触发事件命令的缩写名称（“Comm:”）之后::

	BUG: unable to handle kernel NULL pointer dereference at 0000000000000000
	Oops: 0002 [#1] SMP PTI
	CPU: 0 PID: 4424 Comm: insmod Tainted: P        W  O      4.20.0-0.rc6.fc30 #1
	Hardware name: Red Hat KVM, BIOS 0.5.1 01/01/2011
	RIP: 0010:my_oops_init+0x13/0x1000 [kpanic]
	[...]

如果内核在事件发生时没有被污染，您将在那里看到“Not-tainted:”；如果被污染，那
么它将是“Tainted:”以及字母或空格。在上面的例子中，它看起来是这样的::

	Tainted: P        W  O

下表解释了这些字符的含义。在本例中，由于加载了专有模块（ ``P`` ），出现了
警告（ ``W`` ），并且加载了外部构建的模块（ ``O`` ），所以内核早些时候受到
了污染。要解码其他字符，请使用下表。


解码运行时的污染状态
~~~~~~~~~~~~~~~~~~~~~

在运行时，您可以通过读取 ``cat /proc/sys/kernel/tainted`` 来查询受污染状态。
如果返回 ``0`` ，则内核没有受到污染；任何其他数字都表示受到污染的原因。解码
这个数字的最简单方法是使用脚本  ``tools/debugging/kernel-chktaint`` ，您的
发行版可能会将其作为名为 ``linux-tools`` 或 ``kernel-tools`` 的包的一部分提
供；如果没有，您可以从
`git.kernel.org <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/tools/debugging/kernel-chktaint>`_
网站下载此脚本并用 ``sh kernel-chktaint`` 执行，它会在上面引用的日志中有类似
语句的机器上打印这样的内容::

	Kernel is Tainted for following reasons:
	 * Proprietary module was loaded (#0)
	 * Kernel issued warning (#9)
	 * Externally-built ('out-of-tree') module was loaded  (#12)
	See Documentation/admin-guide/tainted-kernels.rst in the Linux kernel or
	 https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html for
	 a more details explanation of the various taint flags.
	Raw taint value as int/string: 4609/'P        W  O     '

你也可以试着自己解码这个数字。如果内核被污染的原因只有一个，那么这很简单，
在本例中您可以通过下表找到数字。如果你需要解码有多个原因的数字，因为它是一
个位域（bitfield），其中每个位表示一个特定类型的污染的存在或不存在，最好让
前面提到的脚本来处理。但是如果您需要快速看一下，可以使用这个shell命令来检查
设置了哪些位::

	$ for i in $(seq 18); do echo $(($i-1)) $(($(cat /proc/sys/kernel/tainted)>>($i-1)&1));done

污染状态代码表
~~~~~~~~~~~~~~~

===  =====  ======  ========================================================
 位  日志     数字  内核被污染的原因
===  =====  ======  ========================================================
  0   G/P        1  已加载专用模块
  1   _/F        2  模块被强制加载
  2   _/S        4  内核运行在不合规范的系统上
  3   _/R        8  模块被强制卸载
  4   _/M       16  处理器报告了机器检测异常（MCE）
  5   _/B       32  引用了错误的页或某些意外的页标志
  6   _/U       64  用户空间应用程序请求的污染
  7   _/D      128  内核最近死机了，即曾出现OOPS或BUG
  8   _/A      256  ACPI表被用户覆盖
  9   _/W      512  内核发出警告
 10   _/C     1024  已加载staging驱动程序
 11   _/I     2048  已应用平台固件缺陷的解决方案
 12   _/O     4096  已加载外部构建（“树外”）模块
 13   _/E     8192  已加载未签名的模块
 14   _/L    16384  发生软锁定
 15   _/K    32768  内核已实时打补丁
 16   _/X    65536  备用污染，为发行版定义并使用
 17   _/T   131072  内核是用结构随机化插件构建的
===  =====  ======  ========================================================

注：字符 ``_`` 表示空白，以便于阅读表。

污染的更详细解释
~~~~~~~~~~~~~~~~~

 0)  ``G`` 加载的所有模块都有GPL或兼容许可证， ``P`` 加载了任何专有模块。
     没有MODULE_LICENSE（模块许可证）或MODULE_LICENSE未被insmod认可为GPL
     兼容的模块被认为是专有的。


 1)  ``F`` 任何模块被 ``insmod -f`` 强制加载， ``' '`` 所有模块正常加载。

 2)  ``S`` 内核运行在不合规范的处理器或系统上：硬件已运行在不受支持的配置中，
     因此无法保证正确执行。内核将被污染，例如：

     - 在x86上：PAE是通过intel CPU（如Pentium M）上的forcepae强制执行的，这些
       CPU不报告PAE，但可能有功能实现，SMP内核在非官方支持的SMP Athlon CPU上
       运行，MSR被暴露到用户空间中。
     - 在arm上：在某些CPU（如Keystone 2）上运行的内核，没有启用某些内核特性。
     - 在arm64上：CPU之间存在不匹配的硬件特性，引导加载程序以不同的模式引导CPU。
     - 某些驱动程序正在被用在不受支持的体系结构上（例如x86_64以外的其他系统
       上的scsi/snic，非x86/x86_64/itanium上的scsi/ips，已经损坏了arm64上
       irqchip/irq-gic的固件设置…）。

 3)  ``R`` 模块被 ``rmmod -f`` 强制卸载， ``' '`` 所有模块都正常卸载。

 4)  ``M`` 任何处理器报告了机器检测异常， ``' '`` 未发生机器检测异常。

 5)  ``B`` 页面释放函数发现错误的页面引用或某些意外的页面标志。这表示硬件问题
     或内核错误；日志中应该有其他信息指示发生此污染的原因。

 6)  ``U`` 用户或用户应用程序特意请求设置受污染标志，否则应为 ``' '`` 。

 7)  ``D`` 内核最近死机了，即出现了OOPS或BUG。

 8)  ``A`` ACPI表被重写。

 9)  ``W`` 内核之前已发出过警告（尽管有些警告可能会设置更具体的污染标志）。

 10) ``C`` 已加载staging驱动程序。

 11) ``I`` 内核正在处理平台固件（BIOS或类似软件）中的严重错误。

 12) ``O`` 已加载外部构建（“树外”）模块。

 13) ``E`` 在支持模块签名的内核中加载了未签名的模块。

 14) ``L`` 系统上先前发生过软锁定。

 15) ``K`` 内核已经实时打了补丁。

 16) ``X`` 备用污染，由Linux发行版定义和使用。

 17) ``T`` 内核构建时使用了randstruct插件，它可以有意生成非常不寻常的内核结构
     布局（甚至是性能病态的布局），这在调试时非常有用。于构建时设置。
