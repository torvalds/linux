.. highlight:: none

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/dev-tools/gdb-kernel-debugging.rst
:Translator: 高超 gao chao <gaochao49@huawei.com>

通過gdb調試內核和模塊
=====================

Kgdb內核調試器、QEMU等虛擬機管理程序或基於JTAG的硬件接口，支持在運行時使用gdb
調試Linux內核及其模塊。Gdb提供了一個強大的python腳本接口，內核也提供了一套
輔助腳本以簡化典型的內核調試步驟。本文檔爲如何啓用和使用這些腳本提供了一個簡要的教程。
此教程基於QEMU/KVM虛擬機，但文中示例也適用於其他gdb stub。


環境配置要求
------------

- gdb 7.2+ (推薦版本: 7.4+) 且開啓python支持 (通常發行版上都已支持)

設置
----

- 創建一個QEMU/KVM的linux虛擬機（詳情請參考 www.linux-kvm.org 和 www.qemu.org ）。
  對於交叉開發，https://landley.net/aboriginal/bin 提供了一些鏡像和工具鏈，
  可以幫助搭建交叉開發環境。

- 編譯內核時開啓CONFIG_GDB_SCRIPTS，關閉CONFIG_DEBUG_INFO_REDUCED。
  如果架構支持CONFIG_FRAME_POINTER，請保持開啓。

- 在guest環境上安裝該內核。如有必要，通過在內核command line中添加“nokaslr”來關閉KASLR。
  此外，QEMU允許通過-kernel、-append、-initrd這些命令行選項直接啓動內核。
  但這通常僅在不依賴內核模塊時纔有效。有關此模式的更多詳細信息，請參閱QEMU文檔。
  在這種情況下，如果架構支持KASLR，應該在禁用CONFIG_RANDOMIZE_BASE的情況下構建內核。

- 啓用QEMU/KVM的gdb stub，可以通過如下方式實現

    - 在VM啓動時，通過在QEMU命令行中添加“-s”參數

  或

    - 在運行時通過從QEMU監視控制檯發送“gdbserver”

- 切換到/path/to/linux-build(內核源碼編譯)目錄

- 啓動gdb：gdb vmlinux

  注意：某些發行版可能會將gdb腳本的自動加載限制在已知的安全目錄中。
  如果gdb報告拒絕加載vmlinux-gdb.py（相關命令找不到），請將::

    add-auto-load-safe-path /path/to/linux-build

  添加到~/.gdbinit。更多詳細信息，請參閱gdb幫助信息。

- 連接到已啓動的guest環境::

    (gdb) target remote :1234


使用Linux提供的gdb腳本的示例
----------------------------

- 加載模塊（以及主內核）符號::

    (gdb) lx-symbols
    loading vmlinux
    scanning for modules in /home/user/linux/build
    loading @0xffffffffa0020000: /home/user/linux/build/net/netfilter/xt_tcpudp.ko
    loading @0xffffffffa0016000: /home/user/linux/build/net/netfilter/xt_pkttype.ko
    loading @0xffffffffa0002000: /home/user/linux/build/net/netfilter/xt_limit.ko
    loading @0xffffffffa00ca000: /home/user/linux/build/net/packet/af_packet.ko
    loading @0xffffffffa003c000: /home/user/linux/build/fs/fuse/fuse.ko
    ...
    loading @0xffffffffa0000000: /home/user/linux/build/drivers/ata/ata_generic.ko

- 對一些尚未加載的模塊中的函數函數設置斷點，例如::

    (gdb) b btrfs_init_sysfs
    Function "btrfs_init_sysfs" not defined.
    Make breakpoint pending on future shared library load? (y or [n]) y
    Breakpoint 1 (btrfs_init_sysfs) pending.

- 繼續執行::

    (gdb) c

- 加載模塊並且能觀察到正在加載的符號以及斷點命中::

    loading @0xffffffffa0034000: /home/user/linux/build/lib/libcrc32c.ko
    loading @0xffffffffa0050000: /home/user/linux/build/lib/lzo/lzo_compress.ko
    loading @0xffffffffa006e000: /home/user/linux/build/lib/zlib_deflate/zlib_deflate.ko
    loading @0xffffffffa01b1000: /home/user/linux/build/fs/btrfs/btrfs.ko

    Breakpoint 1, btrfs_init_sysfs () at /home/user/linux/fs/btrfs/sysfs.c:36
    36              btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);

- 查看內核的日誌緩衝區::

    (gdb) lx-dmesg
    [     0.000000] Initializing cgroup subsys cpuset
    [     0.000000] Initializing cgroup subsys cpu
    [     0.000000] Linux version 3.8.0-rc4-dbg+ (...
    [     0.000000] Command line: root=/dev/sda2 resume=/dev/sda1 vga=0x314
    [     0.000000] e820: BIOS-provided physical RAM map:
    [     0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usable
    [     0.000000] BIOS-e820: [mem 0x000000000009fc00-0x000000000009ffff] reserved
    ....

- 查看當前task struct結構體的字段（僅x86和arm64支持）::

    (gdb) p $lx_current().pid
    $1 = 4998
    (gdb) p $lx_current().comm
    $2 = "modprobe\000\000\000\000\000\000\000"

- 對當前或指定的CPU使用per-cpu函數::

    (gdb) p $lx_per_cpu(runqueues).nr_running
    $3 = 1
    (gdb) p $lx_per_cpu(runqueues, 2).nr_running
    $4 = 0

- 使用container_of查看更多hrtimers信息::

    (gdb) set $leftmost = $lx_per_cpu(hrtimer_bases).clock_base[0].active.rb_root.rb_leftmost
    (gdb) p *$container_of($leftmost, "struct hrtimer", "node")
    $5 = {
      node = {
        node = {
          __rb_parent_color = 18446612686384860673,
          rb_right = 0xffff888231da8b00,
          rb_left = 0x0
        },
        expires = 1228461000000
      },
      _softexpires = 1228461000000,
      function = 0xffffffff8137ab20 <tick_nohz_handler>,
      base = 0xffff888231d9b4c0,
      state = 1 '\001',
      is_rel = 0 '\000',
      is_soft = 0 '\000',
      is_hard = 1 '\001'
    }


命令和輔助調試功能列表
----------------------

命令和輔助調試功能可能會隨着時間的推移而改進，此文顯示的是初始版本的部分示例::

 (gdb) apropos lx
 function lx_current -- Return current task
 function lx_module -- Find module by name and return the module variable
 function lx_per_cpu -- Return per-cpu variable
 function lx_task_by_pid -- Find Linux task by PID and return the task_struct variable
 function lx_thread_info -- Calculate Linux thread_info from task variable
 lx-dmesg -- Print Linux kernel log buffer
 lx-lsmod -- List currently loaded modules
 lx-symbols -- (Re-)load symbols of Linux kernel and currently loaded modules

可以通過“help <command-name>”或“help function <function-name>”命令
獲取指定命令或指定調試功能的更多詳細信息。

