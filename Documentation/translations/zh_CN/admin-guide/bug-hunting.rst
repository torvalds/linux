.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../admin-guide/bug-hunting`

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

追踪缺陷
=========

内核错误报告通常附带如下堆栈转储::

	------------[ cut here ]------------
	WARNING: CPU: 1 PID: 28102 at kernel/module.c:1108 module_put+0x57/0x70
	Modules linked in: dvb_usb_gp8psk(-) dvb_usb dvb_core nvidia_drm(PO) nvidia_modeset(PO) snd_hda_codec_hdmi snd_hda_intel snd_hda_codec snd_hwdep snd_hda_core snd_pcm snd_timer snd soundcore nvidia(PO) [last unloaded: rc_core]
	CPU: 1 PID: 28102 Comm: rmmod Tainted: P        WC O 4.8.4-build.1 #1
	Hardware name: MSI MS-7309/MS-7309, BIOS V1.12 02/23/2009
	 00000000 c12ba080 00000000 00000000 c103ed6a c1616014 00000001 00006dc6
	 c1615862 00000454 c109e8a7 c109e8a7 00000009 ffffffff 00000000 f13f6a10
	 f5f5a600 c103ee33 00000009 00000000 00000000 c109e8a7 f80ca4d0 c109f617
	Call Trace:
	 [<c12ba080>] ? dump_stack+0x44/0x64
	 [<c103ed6a>] ? __warn+0xfa/0x120
	 [<c109e8a7>] ? module_put+0x57/0x70
	 [<c109e8a7>] ? module_put+0x57/0x70
	 [<c103ee33>] ? warn_slowpath_null+0x23/0x30
	 [<c109e8a7>] ? module_put+0x57/0x70
	 [<f80ca4d0>] ? gp8psk_fe_set_frontend+0x460/0x460 [dvb_usb_gp8psk]
	 [<c109f617>] ? symbol_put_addr+0x27/0x50
	 [<f80bc9ca>] ? dvb_usb_adapter_frontend_exit+0x3a/0x70 [dvb_usb]
	 [<f80bb3bf>] ? dvb_usb_exit+0x2f/0xd0 [dvb_usb]
	 [<c13d03bc>] ? usb_disable_endpoint+0x7c/0xb0
	 [<f80bb48a>] ? dvb_usb_device_exit+0x2a/0x50 [dvb_usb]
	 [<c13d2882>] ? usb_unbind_interface+0x62/0x250
	 [<c136b514>] ? __pm_runtime_idle+0x44/0x70
	 [<c13620d8>] ? __device_release_driver+0x78/0x120
	 [<c1362907>] ? driver_detach+0x87/0x90
	 [<c1361c48>] ? bus_remove_driver+0x38/0x90
	 [<c13d1c18>] ? usb_deregister+0x58/0xb0
	 [<c109fbb0>] ? SyS_delete_module+0x130/0x1f0
	 [<c1055654>] ? task_work_run+0x64/0x80
	 [<c1000fa5>] ? exit_to_usermode_loop+0x85/0x90
	 [<c10013f0>] ? do_fast_syscall_32+0x80/0x130
	 [<c1549f43>] ? sysenter_past_esp+0x40/0x6a
	---[ end trace 6ebc60ef3981792f ]---

这样的堆栈跟踪提供了足够的信息来识别内核源代码中发生错误的那一行。根据问题的
严重性，它还可能包含 **“Oops”** 一词，比如::

	BUG: unable to handle kernel NULL pointer dereference at   (null)
	IP: [<c06969d4>] iret_exc+0x7d0/0xa59
	*pdpt = 000000002258a001 *pde = 0000000000000000
	Oops: 0002 [#1] PREEMPT SMP
	...

尽管有 **Oops** 或其他类型的堆栈跟踪，但通常需要找到出问题的行来识别和处理缺
陷。在本章中，我们将参考“Oops”来了解需要分析的各种堆栈跟踪。

如果内核是用 ``CONFIG_DEBUG_INFO`` 编译的，那么可以使用文件：
`scripts/decode_stacktrace.sh` 。

链接的模块
-----------

受到污染或正在加载/卸载的模块用“（…）”标记，污染标志在
`Documentation/admin-guide/tainted-kernels.rst` 文件中进行了描述，“正在被加
载”用“+”标注，“正在被卸载”用“-”标注。


Oops消息在哪？
---------------

通常，Oops文本由klogd从内核缓冲区读取，然后交给 ``syslogd`` ，后者将其写入
syslog文件，通常是 ``/var/log/messages`` （取决于 ``/etc/syslog.conf`` ）。
在使用systemd的系统上，它也可以由 ``journald`` 守护进程存储，并通过运行
``journalctl`` 命令进行访问。

有时 ``klogd`` 会挂掉，这种情况下您可以运行 ``dmesg > file`` 从内核缓冲区
读取数据并保存它。或者您可以 ``cat /proc/kmsg > file`` ，但是您必须适时
中断以停止传输，因为 ``kmsg`` 是一个“永无止境的文件”。

如果机器严重崩溃，无法输入命令或磁盘不可用，那还有三个选项：

(1) 手动复制屏幕上的文本，并在机器重新启动后输入。很难受，但这是突然崩溃下
    唯一的选择。或者你可以用数码相机拍下屏幕——虽然不那么好，但总比什么都没
    有好。如果消息滚动超出控制台顶部，使用更高分辨率（例如 ``vga=791`` ）
    引导启动将允许您阅读更多文本。（警告：这需要 ``vesafb`` ，因此对“早期”
    的Oppses没有帮助）

(2) 从串口终端启动（参见
    :ref:`Documentation/admin-guide/serial-console.rst <serial_console>` ），
    在另一台机器上运行调制解调器然后用你喜欢的通信程序捕获输出。
    Minicom运行良好。

(3) 使用Kdump（参阅 Documentation/admin-guide/kdump/kdump.rst ），使用
    Documentation/admin-guide/kdump/gdbmacros.txt 中的dmesg gdbmacro从旧内存
    中提取内核环形缓冲区。

找到缺陷位置
-------------

如果你能指出缺陷在内核源代码中的位置，则报告缺陷的效果会非常好。这有两种方法。
通常来说使用 ``gdb`` 会比较容易，不过内核需要用调试信息来预编译。

gdb
^^^^

GNU 调试器（GNU debugger， ``gdb`` ）是从 ``vmlinux`` 文件中找出OOPS的确切
文件和行号的最佳方法。

在使用 ``CONFIG_DEBUG_INFO`` 编译的内核上使用gdb效果最好。可通过运行以下命令
进行设置::

  $ ./scripts/config -d COMPILE_TEST -e DEBUG_KERNEL -e DEBUG_INFO

在用 ``CONFIG_DEBUG_INFO`` 编译的内核上，你可以直接从OOPS复制EIP值::

 EIP:    0060:[<c021e50e>]    Not tainted VLI

并使用GDB来将其翻译成可读形式::

  $ gdb vmlinux
  (gdb) l *0xc021e50e

如果没有启用 ``CONFIG_DEBUG_INFO`` ，则使用OOPS的函数偏移::

 EIP is at vt_ioctl+0xda8/0x1482

并在启用 ``CONFIG_DEBUG_INFO`` 的情况下重新编译内核::

  $ ./scripts/config -d COMPILE_TEST -e DEBUG_KERNEL -e DEBUG_INFO
  $ make vmlinux
  $ gdb vmlinux
  (gdb) l *vt_ioctl+0xda8
  0x1888 is in vt_ioctl (drivers/tty/vt/vt_ioctl.c:293).
  288	{
  289		struct vc_data *vc = NULL;
  290		int ret = 0;
  291
  292		console_lock();
  293		if (VT_BUSY(vc_num))
  294			ret = -EBUSY;
  295		else if (vc_num)
  296			vc = vc_deallocate(vc_num);
  297		console_unlock();

或者若您想要更详细的显示::

  (gdb) p vt_ioctl
  $1 = {int (struct tty_struct *, unsigned int, unsigned long)} 0xae0 <vt_ioctl>
  (gdb) l *0xae0+0xda8

您也可以使用对象文件作为替代::

  $ make drivers/tty/
  $ gdb drivers/tty/vt/vt_ioctl.o
  (gdb) l *vt_ioctl+0xda8

如果你有调用跟踪，类似::

     Call Trace:
      [<ffffffff8802c8e9>] :jbd:log_wait_commit+0xa3/0xf5
      [<ffffffff810482d9>] autoremove_wake_function+0x0/0x2e
      [<ffffffff8802770b>] :jbd:journal_stop+0x1be/0x1ee
      ...

这表明问题可能在 :jbd: 模块中。您可以在gdb中加载该模块并列出相关代码::

  $ gdb fs/jbd/jbd.ko
  (gdb) l *log_wait_commit+0xa3

.. note::

     您还可以对堆栈跟踪处的任何函数调用执行相同的操作，例如::

	 [<f80bc9ca>] ? dvb_usb_adapter_frontend_exit+0x3a/0x70 [dvb_usb]

     上述调用发生的位置可以通过以下方式看到::

	$ gdb drivers/media/usb/dvb-usb/dvb-usb.o
	(gdb) l *dvb_usb_adapter_frontend_exit+0x3a

objdump
^^^^^^^^

要调试内核，请使用objdump并从崩溃输出中查找十六进制偏移，以找到有效的代码/汇
编行。如果没有调试符号，您将看到所示例程的汇编程序代码，但是如果内核有调试
符号，C代码也将可见（调试符号可以在内核配置菜单的hacking项中启用）。例如::

    $ objdump -r -S -l --disassemble net/ipv4/tcp.o

.. note::

   您需要处于内核树的顶层以便此获得您的C文件。

如果您无法访问源代码，仍然可以使用以下方法调试一些崩溃转储（如Dave Miller的
示例崩溃转储输出所示）::

     EIP is at 	+0x14/0x4c0
      ...
     Code: 44 24 04 e8 6f 05 00 00 e9 e8 fe ff ff 8d 76 00 8d bc 27 00 00
     00 00 55 57  56 53 81 ec bc 00 00 00 8b ac 24 d0 00 00 00 8b 5d 08
     <8b> 83 3c 01 00 00 89 44  24 14 8b 45 28 85 c0 89 44 24 18 0f 85

     Put the bytes into a "foo.s" file like this:

            .text
            .globl foo
     foo:
            .byte  .... /* bytes from Code: part of OOPS dump */

     Compile it with "gcc -c -o foo.o foo.s" then look at the output of
     "objdump --disassemble foo.o".

     Output:

     ip_queue_xmit:
         push       %ebp
         push       %edi
         push       %esi
         push       %ebx
         sub        $0xbc, %esp
         mov        0xd0(%esp), %ebp        ! %ebp = arg0 (skb)
         mov        0x8(%ebp), %ebx         ! %ebx = skb->sk
         mov        0x13c(%ebx), %eax       ! %eax = inet_sk(sk)->opt

`scripts/decodecode` 文件可以用来自动完成大部分工作，这取决于正在调试的CPU
体系结构。

报告缺陷
---------

一旦你通过定位缺陷找到了其发生的地方，你可以尝试自己修复它或者向上游报告它。

为了向上游报告，您应该找出用于开发受影响代码的邮件列表。这可以使用 ``get_maintainer.pl`` 。


例如，您在gspca的sonixj.c文件中发现一个缺陷，则可以通过以下方法找到它的维护者::

	$ ./scripts/get_maintainer.pl -f drivers/media/usb/gspca/sonixj.c
	Hans Verkuil <hverkuil@kernel.org> (odd fixer:GSPCA USB WEBCAM DRIVER,commit_signer:1/1=100%)
	Mauro Carvalho Chehab <mchehab@kernel.org> (maintainer:MEDIA INPUT INFRASTRUCTURE (V4L/DVB),commit_signer:1/1=100%)
	Tejun Heo <tj@kernel.org> (commit_signer:1/1=100%)
	Bhaktipriya Shridhar <bhaktipriya96@gmail.com> (commit_signer:1/1=100%,authored:1/1=100%,added_lines:4/4=100%,removed_lines:9/9=100%)
	linux-media@vger.kernel.org (open list:GSPCA USB WEBCAM DRIVER)
	linux-kernel@vger.kernel.org (open list)

请注意它将指出：

- 最后接触源代码的开发人员（如果这是在git树中完成的）。在上面的例子中是Tejun
  和Bhaktipriya（在这个特定的案例中，没有人真正参与这个文件的开发）；
- 驱动维护人员（Hans Verkuil）；
- 子系统维护人员（Mauro Carvalho Chehab）；
- 驱动程序和/或子系统邮件列表（linux-media@vger.kernel.org）；
- Linux内核邮件列表（linux-kernel@vger.kernel.org）。

通常，修复缺陷的最快方法是将它报告给用于开发相关代码的邮件列表（linux-media
ML），抄送驱动程序维护者（Hans）。

如果你完全不知道该把报告寄给谁，且 ``get_maintainer.pl`` 也没有提供任何有用
的信息，请发送到linux-kernel@vger.kernel.org。

感谢您的帮助，这使Linux尽可能稳定:-)

修复缺陷
---------

如果你懂得编程，你不仅可以通过报告错误来帮助我们，还可以提供一个解决方案。
毕竟，开源就是分享你的工作，你不想因为你的天才而被认可吗？

如果你决定这样做，请在制定解决方案后将其提交到上游。

请务必阅读
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>` ，
以帮助您的代码被接受。


---------------------------------------------------------------------------

用 ``klogd`` 进行Oops跟踪的注意事项
------------------------------------

为了帮助Linus和其他内核开发人员， ``klogd`` 对保护故障的处理提供了大量支持。
为了完整支持地址解析，至少应该使用 ``sysklogd`` 包的1.3-pl3版本。

当发生保护故障时， ``klogd`` 守护进程会自动将内核日志消息中的重要地址转换为
它们的等效符号。然后通过 ``klogd`` 使用的任何报告机制来转发这个已翻译的内核
消息。保护错误消息可以直接从消息文件中剪切出来并转发给内核开发人员。

``klogd`` 执行两种类型的地址解析，静态翻译和动态翻译。静态翻译使用System.map
文件。为了进行静态转换， ``klogd`` 守护进程必须能够在守护进程初始化时找到系
统映射文件。有关 ``klogd`` 如何搜索映射文件的信息，请参见klogd手册页。

当使用内核可加载模块时，动态地址转换非常重要。由于内核模块的内存是从内核的
动态内存池中分配的，因此无论是模块的开头还是模块中的函数和符号都没有固定的
位置。

内核支持系统调用，允许程序确定加载哪些模块及其在内存中的位置。klogd守护进程
使用这些系统调用构建了一个符号表，可用于调试可加载内核模块中发生的保护错误。

klogd至少会提供产生保护故障的模块的名称。如果可加载模块的开发人员选择从模块
导出符号信息，则可能会有其他可用的符号信息。

由于内核模块环境可以是动态的，因此当模块环境发生变化时，必须有一种通知
``klogd`` 守护进程的机制。有一些可用的命令行选项允许klogd向当前正在执行的守
护进程发出信号示意应该刷新符号信息。有关更多信息，请参阅 ``klogd`` 手册页。

sysklogd发行版附带了一个补丁，它修改了 ``modules-2.0.0`` 包，以便在加载或
卸载模块时自动向klogd发送信号。应用此补丁基本上可无缝支持调试内核可加载模块
发生的保护故障。

以下是 ``klogd`` 处理的可加载模块中的保护故障示例::

	Aug 29 09:51:01 blizard kernel: Unable to handle kernel paging request at virtual address f15e97cc
	Aug 29 09:51:01 blizard kernel: current->tss.cr3 = 0062d000, %cr3 = 0062d000
	Aug 29 09:51:01 blizard kernel: *pde = 00000000
	Aug 29 09:51:01 blizard kernel: Oops: 0002
	Aug 29 09:51:01 blizard kernel: CPU:    0
	Aug 29 09:51:01 blizard kernel: EIP:    0010:[oops:_oops+16/3868]
	Aug 29 09:51:01 blizard kernel: EFLAGS: 00010212
	Aug 29 09:51:01 blizard kernel: eax: 315e97cc   ebx: 003a6f80   ecx: 001be77b   edx: 00237c0c
	Aug 29 09:51:01 blizard kernel: esi: 00000000   edi: bffffdb3   ebp: 00589f90   esp: 00589f8c
	Aug 29 09:51:01 blizard kernel: ds: 0018   es: 0018   fs: 002b   gs: 002b   ss: 0018
	Aug 29 09:51:01 blizard kernel: Process oops_test (pid: 3374, process nr: 21, stackpage=00589000)
	Aug 29 09:51:01 blizard kernel: Stack: 315e97cc 00589f98 0100b0b4 bffffed4 0012e38e 00240c64 003a6f80 00000001
	Aug 29 09:51:01 blizard kernel:        00000000 00237810 bfffff00 0010a7fa 00000003 00000001 00000000 bfffff00
	Aug 29 09:51:01 blizard kernel:        bffffdb3 bffffed4 ffffffda 0000002b 0007002b 0000002b 0000002b 00000036
	Aug 29 09:51:01 blizard kernel: Call Trace: [oops:_oops_ioctl+48/80] [_sys_ioctl+254/272] [_system_call+82/128]
	Aug 29 09:51:01 blizard kernel: Code: c7 00 05 00 00 00 eb 08 90 90 90 90 90 90 90 90 89 ec 5d c3

