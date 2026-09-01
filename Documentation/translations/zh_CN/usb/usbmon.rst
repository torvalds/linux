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
小写的 ``usbmon`` 指的是内核中的一项功能，用于收集 USB 总线上的 I/O 跟踪
信息。它类似于网络监控工具 ``tcpdump(1)`` 或 Ethereal 使用的数据包套接
字。通常会用 usbdump 或 USBMon（首字母大写）之类的工具来查看 usbmon 生成
的原始跟踪数据。

usbmon 记录的是各个设备驱动向主机控制器驱动（HCD）发出的请求。因此，如果
HCD 自身有 bug，usbmon 输出的跟踪信息就未必能和真实的总线事务一一对应。
这和 tcpdump 的情况类似。

目前实现了两种 API：``text`` 和 ``binary``。二进制 API 通过 ``/dev`` 下的
字符设备提供，是 ABI 的一部分。文本 API 自内核 2.6.35 起已废弃，但为了
兼容和使用方便，至今仍然保留。

如何使用 usbmon 收集原始文本跟踪信息
====================================

与数据包套接字不同，usbmon 还提供了一个输出文本格式跟踪信息的接口。这样
做主要有两个目的：一是在更完善的格式最终确定之前，将其作为工具间通用的跟
踪交换格式；二是在没有工具时也能直接阅读这些信息。

要收集原始文本跟踪信息，按下面的步骤做即可。

1. 准备
-------

挂载 debugfs（内核配置里必须启用它），并加载 usbmon 模块（如果它是以模块
方式构建的）。如果 usbmon 已经编译进内核，这一步就可以省略。

命令示例::

    # mount -t debugfs none /sys/kernel/debug
    # modprobe usbmon
    #

确认 ``usbmon`` 目录下是否有这些条目::

    # ls /sys/kernel/debug/usb/usbmon
    0s  0u  1s  1t  1u  2s  2t  2u  3s  3t  3u  4s  4t  4u
    #

现在，你可以直接用 ``0u`` 捕获所有总线上的数据包，然后跳到第 3 步；也可
以先按第 2 步找出目标设备所在的总线。这样可以把那些持续产生流量的设备过
滤掉。

2. 查找目标设备连接的是哪条总线
-------------------------------

运行 ``cat /sys/kernel/debug/usb/devices``，找到对应设备的 T 行。通常可以通过
厂商字符串来查找。如果有很多相似设备，可以拔掉其中一个，再比较前后两次
``/sys/kernel/debug/usb/devices`` 的输出。T 行里会包含总线编号。

示例::

  T:  Bus=03 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#=  2 Spd=12  MxCh= 0
  D:  Ver= 1.10 Cls=00(>ifc ) Sub=00 Prot=00 MxPS= 8 #Cfgs=  1
  P:  Vendor=0557 ProdID=2004 Rev= 1.00
  S:  Manufacturer=ATEN
  S:  Product=UC100KM V2.00

``Bus=03`` 表示它位于 3 号总线上。或者，也可以查看 ``lsusb`` 的输出，并从
对应条目里找到总线编号。

示例如下::

  Bus 003 Device 002: ID 0557:2004 ATEN UC100KM V2.00


3. 启动 cat 命令
----------------

如果只监听单条总线，执行::

    # cat /sys/kernel/debug/usb/usbmon/3u > /tmp/1.mon.out

否则，如果要监听所有总线，执行::

    # cat /sys/kernel/debug/usb/usbmon/0u > /tmp/1.mon.out

这个进程会一直运行到被终止为止。由于输出通常会很长，最好把它重定向到文件
或其他位置。


4. 在 USB 总线上执行期望的操作
------------------------------

这里做一些会产生 USB 流量的操作即可，比如插入 U 盘、拷贝文件、操作摄像头
等。


5. 停止 cat
-----------

这一步通常按下键盘中断（Control-C）即可完成。

此时，输出文件（本例中为 ``/tmp/1.mon.out``）可以保存下来，通过电子邮件发
送，也可以用文本编辑器查看。如果要用文本编辑器查看，请确保文件大小不会
大到编辑器无法处理。


原始文本数据格式
================

目前支持两种格式：原始的 ``1t`` 格式和 ``1u`` 格式。``1t`` 格式在内核
2.6.21 中已被废弃。``1u`` 格式增加了一些字段，例如 ISO 帧描述符和
``interval``。它生成的行会稍长一些，但除此之外，它是 ``1t`` 格式的完整
超集。

如果程序需要区分上述两种格式，可以查看 ``address`` 字段（见下文）。如果
其中有两个冒号，就是 ``1t`` 格式；否则是 ``1u`` 格式。

任何文本格式的数据都由一系列事件构成，例如 URB 提交、URB 回调和提交错
误。每个事件占一行，由若干以空白符分隔的字段组成。字段数量和位置会随事件
类型变化，但下面这些字段对所有类型都通用：

下面按从左到右的顺序说明这些通用字段：

- URB 标识（URB Tag）。用于标识 URB，通常是 URB 结构体在内核中的地址
  （十六进制），也可能是序号或其他足以唯一标识 URB 的字符串。

- 时间戳（微秒），十进制数字。时间戳的精度取决于可用时钟，所以可能远低于
  1 微秒（例如实现使用 jiffies 时）。

- 事件类型。它表示的是这一行事件的格式，而不是 URB 的类型。可用值为：
  ``S`` 表示提交，``C`` 表示回调，``E`` 表示提交错误。

- ``Address`` 字段（以前称为 ``pipe``）。它包含四个由冒号分隔的字段：URB
  类型及方向、总线号、设备地址和端点号。类型与方向按下面的方式编码：

  == ==   ====================
  Ci Co   控制输入与输出
  Zi Zo   等时输入与输出
  Ii Io   中断输入与输出
  Bi Bo   批量输入与输出
  == ==   ====================

  总线号、设备地址和端点号都是十进制数，但可能有前导零，方便人阅读。

- URB 状态字段。这个字段要么是一个字母，要么是几个用冒号分隔的数字，依次
  表示 URB 状态、``interval``、``start frame`` 和 ``error count``。与
  ``address`` 字段不同，除状态外，其余字段都可能省略。``interval`` 只会在
  中断和等时 URB 中打印；``start frame`` 只会在等时 URB 中打印；错误计数只
  会在等时回调事件中打印。

  状态字段是一个十进制数，有时为负数，对应 URB 的 ``status`` 字段。对于提
  交事件，这个字段本身并无实际语义，但为了便于脚本解析仍会保留。发生错误
  时，这里填的是错误码。

  如果是控制包的提交事件，这个字段里放的不是一组数字，而是 ``Setup Tag``。
  这很容易分辨，因为 ``Setup Tag`` 永远不是数字。所以脚本如果在这里读到一
  组数字，就会继续读取数据长度（等时 URB 除外）；如果读到的是字母之类的内
  容，就要先读取 ``Setup`` 包，再读取数据长度或等时描述符。

- ``Setup`` 包由 5 个字段组成：``bmRequestType``、``bRequest``、``wValue``、
  ``wIndex`` 和 ``wLength``。这些字段由 USB 2.0 规范定义。如果 ``Setup Tag``
  是 ``s``，就可以安全解码这些字段。否则，说明 Setup 包虽然存在，但并未被
  捕获，此时各字段中会填入占位内容。

- 等时传输帧描述符的数量及其内容：
  如果某个等时传输事件带有描述符，会先打印该 URB 的描述符总数，再为每个描
  述符打印一个字段，最多 5 个。每个字段由三个用冒号分隔的十进制数组成，依
  次表示状态（status）、偏移（offset）和长度（length）。对于提交事件，报
  告的是初始长度；对于回调事件，报告的是实际长度。

- 数据长度：对于提交，表示请求的长度；对于回调，表示实际传输的长度。

- 数据标签：即使数据长度非零，usbmon 也不一定会捕获数据。只有标签为
  ``=`` 时，才会有数据字段。

- 数据字段：以大端十六进制格式显示。注意，这些并不是真正的机器字，只是为
  了便于阅读，把字节流按“字”分组显示。因此最后一个字可能只包含 1 到 4 个
  字节。捕获的数据长度是有限的，可能小于数据长度字段中报告的值。对于等时
  输入（Zi）完成事件，如果缓冲区里的接收数据比较稀疏，捕获数据的长度甚至
  可能大于数据长度字段，因为后者只统计实际接收到的字节，而数据字段展示的
  是整个传输缓冲区。



示例：

获取端口状态的输入控制传输::

    d5ea89a0 3575914555 S Ci:1:001:0 s a3 00 0000 0003 0004 4 <
    d5ea89a0 3575914560 C Ci:1:001:0 0 4 = 01050000

向地址为 5 的存储设备发送一个输出批量传输，其中 31 字节的 Bulk 封装用于承
载 SCSI 命令 ``0x28``（``READ_10``）。为便于排版，下面的第一条记录按两行
显示，但实际 usbmon 输出仍是一行::

    dd65f0e8 4128379752 S Bo:1:005:2 -115 31 =
      55534243 ad000000 00800000 80010a28 20000000 20000040
      00000000 000000
    dd65f0e8 4128379808 C Bo:1:005:2 0 31 >

原始二进制格式与 API
====================
API 的整体架构与前文大体相同，只是事件以二进制格式传递。每个事件都通过
下面的结构发送（这个结构名只是为了叙述方便而虚构的）::


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

可以用 ``read(2)``、``ioctl(2)``，或者通过 ``mmap`` 访问缓冲区，从字符设
备接收这些事件。不过，出于兼容性原因，``read(2)`` 只返回前 48 个字节。

字符设备通常命名为 ``/dev/usbmonN``，其中 ``N`` 是 USB 总线号。编号为零的
设备（``/dev/usbmon0``）比较特殊，表示“所有总线”。具体命名策略由 Linux
发行版决定。

如果你手动创建 ``/dev/usbmon0``，请确保它归 root 所有，并且权限为 ``0600``。
否则，非特权用户就能窃听键盘输入流量。

以下 ``MON_IOC_MAGIC`` 为 ``0x92`` 的 ioctl 调用可用：

``MON_IOCQ_URB_LEN``，定义为 ``_IO(MON_IOC_MAGIC, 1)``

该调用返回下一个事件的数据长度。注意大多数事件不包含数据，因此如果它返回
零，并不意味着没有事件。

``MON_IOCG_STATS``，定义为
``_IOR(MON_IOC_MAGIC, 3, struct mon_bin_stats)``

参数是指向以下结构的指针::

  struct mon_bin_stats {
	u32 queued;
	u32 dropped;
  };

成员 ``queued`` 表示当前缓冲区中已经排队的事件数量，而不是自上次重置以来
处理过的事件数量。

成员 ``dropped`` 表示自上次调用 ``MON_IOCG_STATS`` 以来丢失的事件数量。

``MON_IOCT_RING_SIZE``，定义为 ``_IO(MON_IOC_MAGIC, 4)``

此调用设置缓冲区大小。参数是以字节为单位的缓冲区大小。大小可能会向下取整
到下一个块（或页）。如果请求的大小超出当前内核允许的范围，则调用会失败并
返回 ``-EINVAL``。

``MON_IOCQ_RING_SIZE``，定义为 ``_IO(MON_IOC_MAGIC, 5)``

该调用返回缓冲区当前大小（以字节为单位）。

``MON_IOCX_GET``，定义为
``_IOW(MON_IOC_MAGIC, 6, struct mon_get_arg)``
``MON_IOCX_GETX``，定义为
``_IOW(MON_IOC_MAGIC, 10, struct mon_get_arg)``

如果内核缓冲区中没有事件，这些调用就会一直等待，直到有事件到达，然后返回
第一个事件。
参数是指向以下结构的指针::

  struct mon_get_arg {
	struct usbmon_packet *hdr;
	void *data;
	size_t alloc;		/* 数据长度可以为零 */
  };


调用前，应填好 ``hdr``、``data`` 和 ``alloc``。调用返回后，``hdr`` 指向的
内存区域中会写入下一个事件的结构；如果存在数据，数据缓冲区中也会填入相应
内容。该事件会从内核缓冲区中移除。

``MON_IOCX_GET`` 会将 48 字节的数据复制到 ``hdr`` 区域，``MON_IOCX_GETX``
会复制 64 字节。

``MON_IOCX_MFETCH``，定义为
``_IOWR(MON_IOC_MAGIC, 7, struct mon_mfetch_arg)``

应用程序通过 ``mmap(2)`` 访问缓冲区时，主要使用这个 ioctl。其参数是指向
以下结构的指针::

  struct mon_mfetch_arg {
	uint32_t *offvec;	/* 获取的事件偏移向量 */
	uint32_t nfetch;	/* 要获取的事件数量（输出：已获取） */
	uint32_t nflush;	/* 要刷新的事件数量 */
  };


这个 ioctl 的流程分为三个阶段：

首先，从内核缓冲区移除并丢弃最多 ``nflush`` 个事件。实际丢弃的事件数量会
写回 ``nflush``。

其次，除非设备以 ``O_NONBLOCK`` 打开，否则会一直等待，直到缓冲区中出现
事件。

第三，将最多 ``nfetch`` 个偏移量提取到 mmap 缓冲区，并存入 ``offvec`` 中。
实际提取到的事件偏移数量会写回 ``nfetch``。

``MON_IOCH_MFLUSH``，定义为 ``_IO(MON_IOC_MAGIC, 8)``

此调用从内核缓冲区移除若干事件。其参数是要移除的事件数量。如果缓冲区中的
事件少于请求数量，则移除全部现有事件，且不报告错误。即使当前没有事件，也
可以调用。

``FIONBIO``

如果有需要，将来可能会实现 ``FIONBIO`` ioctl。

除了 ``ioctl(2)`` 和 ``read(2)`` 之外，二进制 API 对应的特殊文件还可以用
``select(2)`` 和 ``poll(2)`` 轮询，但 ``lseek(2)`` 不可用。

* 二进制 API 的内核缓冲区内存映射访问

基本思路很简单：

准备时，先查询当前大小，再用 ``mmap(2)`` 映射缓冲区。之后运行与下面伪代码
类似的循环::

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
         hdr = (struct usbmon_packet *) &mmap_area[vec[i]];
         if (hdr->type == '@')     // 填充包
            continue;
         caddr_t data = &mmap_area[vec[i]] + 64;
         process_packet(hdr, data);
      }
   }



因此，这里的核心思路就是每 N 个事件只执行一次 ioctl。

虽然缓冲区是环形的，但返回的头部和数据不会跨越缓冲区末端，因此上面的伪代
码无需做任何拼接。
