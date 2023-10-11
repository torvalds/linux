.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../admin-guide/bug-hunting`

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <src.res.211@gmail.com>

追蹤缺陷
=========

內核錯誤報告通常附帶如下堆棧轉儲::

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

這樣的堆棧跟蹤提供了足夠的信息來識別內核源代碼中發生錯誤的那一行。根據問題的
嚴重性，它還可能包含 **“Oops”** 一詞，比如::

	BUG: unable to handle kernel NULL pointer dereference at   (null)
	IP: [<c06969d4>] iret_exc+0x7d0/0xa59
	*pdpt = 000000002258a001 *pde = 0000000000000000
	Oops: 0002 [#1] PREEMPT SMP
	...

儘管有 **Oops** 或其他類型的堆棧跟蹤，但通常需要找到出問題的行來識別和處理缺
陷。在本章中，我們將參考“Oops”來了解需要分析的各種堆棧跟蹤。

如果內核是用 ``CONFIG_DEBUG_INFO`` 編譯的，那麼可以使用文件：
`scripts/decode_stacktrace.sh` 。

鏈接的模塊
-----------

受到污染或正在加載/卸載的模塊用“（…）”標記，污染標誌在
`Documentation/admin-guide/tainted-kernels.rst` 文件中進行了描述，“正在被加
載”用“+”標註，“正在被卸載”用“-”標註。


Oops消息在哪？
---------------

通常，Oops文本由klogd從內核緩衝區讀取，然後交給 ``syslogd`` ，後者將其寫入
syslog文件，通常是 ``/var/log/messages`` （取決於 ``/etc/syslog.conf`` ）。
在使用systemd的系統上，它也可以由 ``journald`` 守護進程存儲，並通過運行
``journalctl`` 命令進行訪問。

有時 ``klogd`` 會掛掉，這種情況下您可以運行 ``dmesg > file`` 從內核緩衝區
讀取數據並保存它。或者您可以 ``cat /proc/kmsg > file`` ，但是您必須適時
中斷以停止傳輸，因爲 ``kmsg`` 是一個“永無止境的文件”。

如果機器嚴重崩潰，無法輸入命令或磁盤不可用，那還有三個選項：

(1) 手動複製屏幕上的文本，並在機器重新啓動後輸入。很難受，但這是突然崩潰下
    唯一的選擇。或者你可以用數碼相機拍下屏幕——雖然不那麼好，但總比什麼都沒
    有好。如果消息滾動超出控制檯頂部，使用更高分辨率（例如 ``vga=791`` ）
    引導啓動將允許您閱讀更多文本。（警告：這需要 ``vesafb`` ，因此對“早期”
    的Oppses沒有幫助）

(2) 從串口終端啓動（參見
    :ref:`Documentation/admin-guide/serial-console.rst <serial_console>` ），
    在另一臺機器上運行調制解調器然後用你喜歡的通信程序捕獲輸出。
    Minicom運行良好。

(3) 使用Kdump（參閱 Documentation/admin-guide/kdump/kdump.rst ），使用
    Documentation/admin-guide/kdump/gdbmacros.txt 中的dmesg gdbmacro從舊內存
    中提取內核環形緩衝區。

找到缺陷位置
-------------

如果你能指出缺陷在內核源代碼中的位置，則報告缺陷的效果會非常好。這有兩種方法。
通常來說使用 ``gdb`` 會比較容易，不過內核需要用調試信息來預編譯。

gdb
^^^^

GNU 調試器（GNU debugger， ``gdb`` ）是從 ``vmlinux`` 文件中找出OOPS的確切
文件和行號的最佳方法。

在使用 ``CONFIG_DEBUG_INFO`` 編譯的內核上使用gdb效果最好。可通過運行以下命令
進行設置::

  $ ./scripts/config -d COMPILE_TEST -e DEBUG_KERNEL -e DEBUG_INFO

在用 ``CONFIG_DEBUG_INFO`` 編譯的內核上，你可以直接從OOPS複製EIP值::

 EIP:    0060:[<c021e50e>]    Not tainted VLI

並使用GDB來將其翻譯成可讀形式::

  $ gdb vmlinux
  (gdb) l *0xc021e50e

如果沒有啓用 ``CONFIG_DEBUG_INFO`` ，則使用OOPS的函數偏移::

 EIP is at vt_ioctl+0xda8/0x1482

並在啓用 ``CONFIG_DEBUG_INFO`` 的情況下重新編譯內核::

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

或者若您想要更詳細的顯示::

  (gdb) p vt_ioctl
  $1 = {int (struct tty_struct *, unsigned int, unsigned long)} 0xae0 <vt_ioctl>
  (gdb) l *0xae0+0xda8

您也可以使用對象文件作爲替代::

  $ make drivers/tty/
  $ gdb drivers/tty/vt/vt_ioctl.o
  (gdb) l *vt_ioctl+0xda8

如果你有調用跟蹤，類似::

     Call Trace:
      [<ffffffff8802c8e9>] :jbd:log_wait_commit+0xa3/0xf5
      [<ffffffff810482d9>] autoremove_wake_function+0x0/0x2e
      [<ffffffff8802770b>] :jbd:journal_stop+0x1be/0x1ee
      ...

這表明問題可能在 :jbd: 模塊中。您可以在gdb中加載該模塊並列出相關代碼::

  $ gdb fs/jbd/jbd.ko
  (gdb) l *log_wait_commit+0xa3

.. note::

     您還可以對堆棧跟蹤處的任何函數調用執行相同的操作，例如::

	 [<f80bc9ca>] ? dvb_usb_adapter_frontend_exit+0x3a/0x70 [dvb_usb]

     上述調用發生的位置可以通過以下方式看到::

	$ gdb drivers/media/usb/dvb-usb/dvb-usb.o
	(gdb) l *dvb_usb_adapter_frontend_exit+0x3a

objdump
^^^^^^^^

要調試內核，請使用objdump並從崩潰輸出中查找十六進制偏移，以找到有效的代碼/匯
編行。如果沒有調試符號，您將看到所示例程的彙編程序代碼，但是如果內核有調試
符號，C代碼也將可見（調試符號可以在內核配置菜單的hacking項中啓用）。例如::

    $ objdump -r -S -l --disassemble net/dccp/ipv4.o

.. note::

   您需要處於內核樹的頂層以便此獲得您的C文件。

如果您無法訪問源代碼，仍然可以使用以下方法調試一些崩潰轉儲（如Dave Miller的
示例崩潰轉儲輸出所示）::

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

`scripts/decodecode` 文件可以用來自動完成大部分工作，這取決於正在調試的CPU
體系結構。

報告缺陷
---------

一旦你通過定位缺陷找到了其發生的地方，你可以嘗試自己修復它或者向上遊報告它。

爲了向上遊報告，您應該找出用於開發受影響代碼的郵件列表。這可以使用 ``get_maintainer.pl`` 。


例如，您在gspca的sonixj.c文件中發現一個缺陷，則可以通過以下方法找到它的維護者::

	$ ./scripts/get_maintainer.pl -f drivers/media/usb/gspca/sonixj.c
	Hans Verkuil <hverkuil@xs4all.nl> (odd fixer:GSPCA USB WEBCAM DRIVER,commit_signer:1/1=100%)
	Mauro Carvalho Chehab <mchehab@kernel.org> (maintainer:MEDIA INPUT INFRASTRUCTURE (V4L/DVB),commit_signer:1/1=100%)
	Tejun Heo <tj@kernel.org> (commit_signer:1/1=100%)
	Bhaktipriya Shridhar <bhaktipriya96@gmail.com> (commit_signer:1/1=100%,authored:1/1=100%,added_lines:4/4=100%,removed_lines:9/9=100%)
	linux-media@vger.kernel.org (open list:GSPCA USB WEBCAM DRIVER)
	linux-kernel@vger.kernel.org (open list)

請注意它將指出：

- 最後接觸源代碼的開發人員（如果這是在git樹中完成的）。在上面的例子中是Tejun
  和Bhaktipriya（在這個特定的案例中，沒有人真正參與這個文件的開發）；
- 驅動維護人員（Hans Verkuil）；
- 子系統維護人員（Mauro Carvalho Chehab）；
- 驅動程序和/或子系統郵件列表（linux-media@vger.kernel.org）；
- Linux內核郵件列表（linux-kernel@vger.kernel.org）。

通常，修復缺陷的最快方法是將它報告給用於開發相關代碼的郵件列表（linux-media
ML），抄送驅動程序維護者（Hans）。

如果你完全不知道該把報告寄給誰，且 ``get_maintainer.pl`` 也沒有提供任何有用
的信息，請發送到linux-kernel@vger.kernel.org。

感謝您的幫助，這使Linux儘可能穩定:-)

修復缺陷
---------

如果你懂得編程，你不僅可以通過報告錯誤來幫助我們，還可以提供一個解決方案。
畢竟，開源就是分享你的工作，你不想因爲你的天才而被認可嗎？

如果你決定這樣做，請在制定解決方案後將其提交到上游。

請務必閱讀
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>` ，
以幫助您的代碼被接受。


---------------------------------------------------------------------------

用 ``klogd`` 進行Oops跟蹤的注意事項
------------------------------------

爲了幫助Linus和其他內核開發人員， ``klogd`` 對保護故障的處理提供了大量支持。
爲了完整支持地址解析，至少應該使用 ``sysklogd`` 包的1.3-pl3版本。

當發生保護故障時， ``klogd`` 守護進程會自動將內核日誌消息中的重要地址轉換爲
它們的等效符號。然後通過 ``klogd`` 使用的任何報告機制來轉發這個已翻譯的內核
消息。保護錯誤消息可以直接從消息文件中剪切出來並轉發給內核開發人員。

``klogd`` 執行兩種類型的地址解析，靜態翻譯和動態翻譯。靜態翻譯使用System.map
文件。爲了進行靜態轉換， ``klogd`` 守護進程必須能夠在守護進程初始化時找到系
統映射文件。有關 ``klogd`` 如何搜索映射文件的信息，請參見klogd手冊頁。

當使用內核可加載模塊時，動態地址轉換非常重要。由於內核模塊的內存是從內核的
動態內存池中分配的，因此無論是模塊的開頭還是模塊中的函數和符號都沒有固定的
位置。

內核支持系統調用，允許程序確定加載哪些模塊及其在內存中的位置。klogd守護進程
使用這些系統調用構建了一個符號表，可用於調試可加載內核模塊中發生的保護錯誤。

klogd至少會提供產生保護故障的模塊的名稱。如果可加載模塊的開發人員選擇從模塊
導出符號信息，則可能會有其他可用的符號信息。

由於內核模塊環境可以是動態的，因此當模塊環境發生變化時，必須有一種通知
``klogd`` 守護進程的機制。有一些可用的命令行選項允許klogd向當前正在執行的守
護進程發出信號示意應該刷新符號信息。有關更多信息，請參閱 ``klogd`` 手冊頁。

sysklogd發行版附帶了一個補丁，它修改了 ``modules-2.0.0`` 包，以便在加載或
卸載模塊時自動向klogd發送信號。應用此補丁基本上可無縫支持調試內核可加載模塊
發生的保護故障。

以下是 ``klogd`` 處理的可加載模塊中的保護故障示例::

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

---------------------------------------------------------------------------

::

  Dr. G.W. Wettstein           Oncology Research Div. Computing Facility
  Roger Maris Cancer Center    INTERNET: greg@wind.rmcc.com
  820 4th St. N.
  Fargo, ND  58122
  Phone: 701-234-7556

