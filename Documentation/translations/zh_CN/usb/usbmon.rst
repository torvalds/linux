.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/usb/usbmon.rst

:翻译:

 白钶凡 Kefan Bai <baikefan@leap-io-kernel.com>

:校译:


======
usbmon
======

简介
====
小写形式的 ``usbmon`` 指的是内核中的一项功能，
用于收集 USB 总线上的 I/O 跟踪信息。它类似于网络监控工具
``tcpdump(1)`` 或 Ethereal 所使用的数据包套接字。
类似地，人们希望使用 usbdump 或 USBMon
（首字母大写）之类的工具来检查
usbmon 生成的原始跟踪数据。

usbmon 报告的是各个外设驱动
向主机控制器驱动（HCD）发出的请求。
因此，如果 HCD 本身有 bug，那么 usbmon 报告的跟踪信息
可能无法精确对应实际的总线事务。
这和 tcpdump 的情况是一样的。

目前实现了两种 API: ``text`` 和 ``binary``。
二进制 API 通过 ``/dev`` 命名空间中的字符设备提供，
并且属于 ABI。文本 API 自内核 2.6.35 起已废弃，
但为了方便仍然可用。

如何使用 usbmon 收集原始文本跟踪信息
====================================

与数据包套接字不同，usbmon 提供了一种接口，
可以输出文本格式的跟踪信息。这样做有两个目的：
第一，在更完善的格式最终确定之前，
它作为工具间通用的跟踪交换格式；
第二，在不使用工具的情况下，人们也可以直接阅读这些信息。

要收集原始文本跟踪信息，请按以下步骤进行操作。

1. 准备
-------

挂载 debugfs（内核配置中必须启用它），并加载 usbmon 模块
（如果它是作为模块构建的）。如果 usbmon 已经编入内核，
那么第二步可以省略。

命令示例::

    # mount -t debugfs none_debugs /sys/kernel/debug
    # modprobe usbmon
    #

确认总线套接字是否存在::

    # ls /sys/kernel/debug/usb/usbmon
    0s  0u  1s  1t  1u  2s  2t  2u  3s  3t  3u  4s  4t  4u
    #

现在，你可以选择使用 ``0u`` 捕获所有总线上的数据包，
并跳到第 3 步；
也可以先按第 2 步找到目标设备所在的总线。
这样可以过滤掉那些持续输出数据的烦人设备。

2. 查找目标设备连接的是哪条总线
-------------------------------

运行 ``cat /sys/kernel/debug/usb/devices``，
找到对应设备的 T 行。通常可以通过厂商字符串来查找。
如果有许多类似设备，可以拔掉其中一个，
再比较前后两次 ``/sys/kernel/debug/usb/devices``
的输出。T 行里会包含总线编号。

示例::

  T:  Bus=03 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#=  2 Spd=12  MxCh= 0
  D:  Ver= 1.10 Cls=00(>ifc ) Sub=00 Prot=00 MxPS= 8 #Cfgs=  1
  P:  Vendor=0557 ProdID=2004 Rev= 1.00
  S:  Manufacturer=ATEN
  S:  Product=UC100KM V2.00

``Bus=03`` 表示它位于 3 号总线上。或者，
也可以查看 ``lsusb`` 的输出，并从对应行得到总线编号。

示例如下::

  Bus 003 Device 002: ID 0557:2004 ATEN UC100KM V2.00


3. 启动 cat 命令
----------------

如果只监听单条总线，可执行::

    # cat /sys/kernel/debug/usb/usbmon/3u > /tmp/1.mon.out

否则，如果要监听所有总线，则执行::

    # cat /sys/kernel/debug/usb/usbmon/0u > /tmp/1.mon.out

此进程会一直读取，直到被终止。
由于输出通常会很长，因此更推荐将输出重定向到某个位置。


4. 在 USB 总线上执行期望的操作
------------------------------

此处需要执行一些会产生 USB 流量的动作，
比如插入 U 盘、拷贝文件、操作摄像头等。


5. 停止 cat
-----------

这一步通常通过键盘中断（Control-C）完成。

此时输出文件（本例中为 ``/tmp/1.mon.out``）
可以保存、通过电子邮件发送，或使用文本编辑器查看。
如果使用最后一种方式，请确保文件不会大到编辑器无法打开。


原始文本数据格式
================

目前支持两种格式：原始格式，也就是 ``1t`` 格式，
以及 ``1u`` 格式。``1t`` 格式在内核 2.6.21 中已被废弃。
``1u`` 格式增加了一些字段，例如 ISO 帧描述符、
``interval`` 等。它生成的行会稍长一些，
但在其他方面是 ``1t`` 格式的完整超集。

如果程序需要区分上述两种格式，
可以查看 ``address`` 字段（见下文）。
如果其中有两个冒号，就是 ``1t`` 格式；
否则是 ``1u`` 格式。

任何文本格式的数据由一系列事件组成，
如 URB 提交、URB 回调、提交错误等。
每个事件对应单独的一行文本，
由使用空白符间隔的若干字段组成。
字段的数量与位置可能取决于事件类型，
但以下字段对所有类型都通用：

下面按从左到右的顺序列出这些共有字段：

- URB Tag。用于标识 URB，通常是 URB 结构体在内核中的地址
  （以十六进制表示），
  但也可能是序号或其他合理的唯一字符串。

- 时间戳（微秒），十进制数字。
  时间戳的精度取决于可用时钟，
  因此可能远差于
  1 微秒（例如实现使用的是 jiffies）。

- 事件类型。它表示的是事件的格式，而不是 URB 的类型。
  可用值为：``S`` 表示提交，``C`` 表示回调，``E`` 表示提交错误。

- ``Address`` 字段（以前称作 ``pipe``）。
  它包含四个由冒号分隔的字段：
  URB 类型及方向、总线号、设备地址和端点号。类型与方向的编码如下：

    == ==   ==========================
    Ci Co   控制输入和输出
    Zi Zo   等时输入和输出
    Ii Io   中断输入和输出
    Bi Bo   批量输入和输出
    == ==   ==========================

  总线号、设备地址和端点号使用十进制，但可能有前导零。

- URB 状态字段。这个字段要么是一个字母，
  要么是几个由冒号分隔的数字：
  URB 状态、``interval``、``start frame`` 和 ``error count``。
  与 ``address`` 字段不同，除了状态外，其余字段都是可选的。
  ``interval`` 只会为中断和等时 URB 打印；``start frame`` 只会为
  等时 URB 打印；错误计数只会在等时回调事件中打印。

  状态字段是一个十进制数字，有时为负数，
  对应 URB 的 ``status`` 字段。
  对于提交事件，这个字段本身没有实际意义，
  但为了便于脚本解析，它仍然存在。
  当发生错误时，该字段包含错误码。

  在提交控制包时，这个字段包含的是 ``Setup Tag``，
  而不是一组数字。
  判断 ``Setup Tag`` 是否存在很容易，因为它从来不是数字。
  因此，如果脚本在这个字段里发现的是一组数字，
  就会继续读取数据长度（等时 URB 除外）。
  如果发现的是其他内容，比如一个字母，
  那么脚本会先读取 ``Setup`` 包，再读取数据长度或等时描述符。

- ``Setup`` 包由 5 个字段组成：
  ``bmRequestType``、``bRequest``、``wValue``、
  ``wIndex`` 和 ``wLength``。这些字段由 USB 2.0 规范定义。
  如果 ``Setup Tag`` 为 ``s``，就可以安全地解码这些字段。
  否则，说明 Setup 包虽然存在，但并未被捕获，此时各字段中会填入占位内容。

- 等时传输帧描述符的数量及其内容：
  如果一个等时传输事件带有一组描述符，首先打印该 URB 中描述符的总数，
  然后为每个描述符打印一个字段，最多打印 5 个字段。
  每个字段由三个用冒号分隔的十进制数字组成，
  分别表示状态（status）、偏移（offset）和长度（length）。
  对于提交（submission），报告的是初始长度；
  对于回调（callback），报告的是实际长度。

- 数据长度：
  对于提交，表示请求的长度；对于回调，表示实际传输的长度。

- 数据标签：
  即使数据长度非零，usbmon 也不一定会捕获数据。
  仅当标签为 ``=`` 时，才会有数据字段。

- 数据字段：
  以大端十六进制格式显示。注意，这些并不是真正的机器字，
  而只是把字节流拆成若干“字”以便阅读。因此最后一个字可能只包含
  1 到 4 个字节。
  收集的数据长度是有限的，可能小于数据长度字段中报告的值。
  因为数据长度字段只统计实际接收到的字节，而数据字段包含整个传输缓冲区，
  所以，在等时输入（Zi）完成且缓冲区中接收到的数据稀疏的情况下，
  收集的数据长度可能大于数据长度字段的值。



示例：

获取端口状态的输入控制传输::

    d5ea89a0 3575914555 S Ci:1:001:0 s a3 00 0000 0003 0004 4 <
    d5ea89a0 3575914560 C Ci:1:001:0 0 4 = 01050000

向地址为 5 的存储设备发送
31 字节 Bulk 包装的 SCSI 命令 ``0x28``
（``READ_10``）的输出批量传输::

    dd65f0e8 4128379752 S Bo:1:005:2 -115 31 = 55534243 ad000000 00800000 80010a28 20000000 20000040 00000000 000000
    dd65f0e8 4128379808 C Bo:1:005:2 0 31 >

原始二进制格式与 API
====================
API 的整体架构与前文大体相同，只是事件以二进制格式传递。
每个事件都通过下面的结构发送
（这个名字是为了叙述方便而虚构的）::


  struct usbmon_packet {
	u64 id;			/*  0: URB ID - 从提交到回调 */
	unsigned char type;	/*  8: 与文本相同；可扩展 */
	unsigned char xfer_type; /*    ISO (0)、中断、控制、批量 (3) */
	unsigned char epnum;	/*     端点号和传输方向 */
	unsigned char devnum;	/*     设备地址 */
	u16 busnum;		/* 12: 总线号 */
	char flag_setup;	/* 14: 与文本相同 */
	char flag_data;		/* 15: 与文本相同；二进制零也可 */
	s64 ts_sec;		/* 16: gettimeofday */
	s32 ts_usec;		/* 24: gettimeofday */
	int status;		/* 28: */
	unsigned int length;	/* 32: 数据长度（提交或实际） */
	unsigned int len_cap;	/* 36: 已捕获的数据长度 */
	union {			/* 40: */
		unsigned char setup[SETUP_LEN];	/* 仅用于控制类 S 事件 */
		struct iso_rec {		/* 仅用于 ISO */
			int error_count;
			int numdesc;
		} iso;
	} s;
	int interval;		/* 48: 仅用于中断和 ISO */
	int start_frame;	/* 52: 仅用于 ISO */
	unsigned int xfer_flags; /* 56: URB 的 transfer_flags 副本 */
	unsigned int ndesc;	/* 60: 实际 ISO 描述符数量 */
  };				/* 64 总长度 */

可以用 ``read(2)``、``ioctl(2)``，
或者通过 ``mmap`` 访问缓冲区，
从字符设备接收这些事件。
不过，出于兼容性原因，``read(2)``
只返回前 48 个字节。

字符设备通常命名为 ``/dev/usbmonN``，
其中 ``N`` 是 USB 总线号。
编号为零的设备（``/dev/usbmon0``）比较特殊，
表示“所有总线”。
请注意，具体命名策略由 Linux 发行版决定。

如果你手动创建 ``/dev/usbmon0``，
请确保它归 root 所有，并且权限为 ``0600``。
否则，非特权用户将能够窃听键盘流量。

以下 ``MON_IOC_MAGIC`` 为 ``0x92`` 的 ioctl 调用可用：

``MON_IOCQ_URB_LEN``，定义为 ``_IO(MON_IOC_MAGIC, 1)``

该调用返回下一个事件的数据长度。
注意大多数事件不包含数据，
因此如果该调用返回零，并不意味着没有事件。

``MON_IOCG_STATS``，定义为
``_IOR(MON_IOC_MAGIC, 3, struct mon_bin_stats)``

参数是指向以下结构的指针::

  struct mon_bin_stats {
	u32 queued;
	u32 dropped;
  };

成员 ``queued`` 表示当前缓冲区中已经排队的事件数量，
而不是自上次重置以来处理过的事件数量。

成员 ``dropped`` 表示自上次调用
``MON_IOCG_STATS`` 以来丢失的事件数量。

``MON_IOCT_RING_SIZE``，定义为 ``_IO(MON_IOC_MAGIC, 4)``

此调用设置缓冲区大小。参数为以字节为单位的缓冲区大小。
大小可能会向下取整到下一个块（或页）。
如果请求的大小超出该内核的 [未指定] 范围，
则调用会失败并返回 ``-EINVAL``。

``MON_IOCQ_RING_SIZE``，定义为 ``_IO(MON_IOC_MAGIC, 5)``

该调用返回缓冲区当前大小（以字节为单位）。

``MON_IOCX_GET``，定义为
``_IOW(MON_IOC_MAGIC, 6, struct mon_get_arg)``
``MON_IOCX_GETX``，定义为
``_IOW(MON_IOC_MAGIC, 10, struct mon_get_arg)``

如果内核缓冲区中没有事件，
这些调用就会一直等待，直到有事件到达，
然后返回第一个事件。
参数是指向以下结构的指针::

  struct mon_get_arg {
	struct usbmon_packet *hdr;
	void *data;
	size_t alloc;		/* 数据长度可以为零 */
  };


调用前，应填好 ``hdr``、``data`` 和 ``alloc``。
调用返回后，``hdr`` 指向的区域中包含下一个事件的结构；
如果存在数据，那么数据缓冲区中也会包含相应数据。
该事件会从内核缓冲区中移除。

``MON_IOCX_GET`` 会将 48 字节的数据复制到 ``hdr`` 区域，
``MON_IOCX_GETX`` 会复制 64 字节。

``MON_IOCX_MFETCH``，定义为
``_IOWR(MON_IOC_MAGIC, 7, struct mon_mfetch_arg)``

当应用程序通过 ``mmap(2)`` 访问缓冲区时，
主要使用这个 ioctl。
其参数是指向以下结构的指针::

  struct mon_mfetch_arg {
	uint32_t *offvec;	/* 获取的事件偏移向量 */
	uint32_t nfetch;	/* 要获取的事件数量（输出：已获取） */
	uint32_t nflush;	/* 要刷新的事件数量 */
  };


该 ioctl 的操作分为三个阶段：

首先，从内核缓冲区移除并丢弃最多 ``nflush`` 个事件。
实际丢弃的事件数量会写回 ``nflush``。

其次，除非伪设备以 ``O_NONBLOCK`` 打开，否则会一直等待，
直到缓冲区中出现事件。

第三，将最多 ``nfetch`` 个偏移量提取到 mmap
缓冲区，并存入 ``offvec`` 中。
实际提取到的事件偏移数量会存回 ``nfetch``。

``MON_IOCH_MFLUSH``，定义为 ``_IO(MON_IOC_MAGIC, 8)``

此调用从内核缓冲区移除若干事件。
其参数为要移除的事件数量。
如果缓冲区中的事件少于请求数量，
则移除所有事件，且不报告错误。
当没有事件时也可使用。

``FIONBIO``

如果有需要，将来可能会实现 ``FIONBIO`` ioctl。

除了 ``ioctl(2)`` 和 ``read(2)`` 之外，
二进制 API 的特殊文件也可以用 ``select(2)`` 和
``poll(2)`` 轮询。
但 ``lseek(2)`` 不起作用。

* 二进制 API 的内核缓冲区内存映射访问

基本思想很简单：

准备时，先获取当前大小，再用 ``mmap(2)`` 映射缓冲区。
然后执行类似下面伪代码的循环::

   struct mon_mfetch_arg fetch;
   struct usbmon_packet *hdr;
   int nflush = 0;
   for (;;) {
      fetch.offvec = vec; // 有 N 个 32 位字
      fetch.nfetch = N;   // 或者少于 N
      fetch.nflush = nflush;
      ioctl(fd, MON_IOCX_MFETCH, &fetch);   // 同时处理错误
      nflush = fetch.nfetch;       // 完成后要刷新这么多包
      for (i = 0; i < nflush; i++) {
         hdr = (struct ubsmon_packet *) &mmap_area[vec[i]];
         if (hdr->type == '@')     // 填充包
            continue;
         caddr_t data = &mmap_area[vec[i]] + 64;
         process_packet(hdr, data);
      }
   }



因此，主要思想是每 N 个事件只执行一次 ioctl。

虽然缓冲区是环形的，但返回的头和数据不会跨越缓冲区末端，
因此上面的伪代码无需任何合并操作。
