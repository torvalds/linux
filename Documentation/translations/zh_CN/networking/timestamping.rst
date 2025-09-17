.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/timestamping.rst

:翻译:

   王亚鑫 Wang Yaxin <wang.yaxin@zte.com.cn>

======
时间戳
======


1. 控制接口
===========

接收网络数据包时间戳的接口包括：

SO_TIMESTAMP
  为每个传入数据包生成（不一定是单调的）系统时间时间戳。通过 recvmsg()
  在控制消息中以微秒分辨率报告时间戳。
  SO_TIMESTAMP 根据架构类型和 libc 的 lib 中的 time_t 表示方式定义为
  SO_TIMESTAMP_NEW 或 SO_TIMESTAMP_OLD。
  SO_TIMESTAMP_OLD 和 SO_TIMESTAMP_NEW 的控制消息格式分别为
  struct __kernel_old_timeval 和 struct __kernel_sock_timeval。

SO_TIMESTAMPNS
  与 SO_TIMESTAMP 相同的时间戳机制，但以 struct timespec 格式报告时间戳，
  纳秒分辨率。
  SO_TIMESTAMPNS 根据架构类型和 libc 的 time_t 表示方式定义为
  SO_TIMESTAMPNS_NEW 或 SO_TIMESTAMPNS_OLD。
  控制消息格式对于 SO_TIMESTAMPNS_OLD 为 struct timespec，
  对于 SO_TIMESTAMPNS_NEW 为 struct __kernel_timespec。

IP_MULTICAST_LOOP + SO_TIMESTAMP[NS]
  仅用于多播：通过读取回环数据包接收时间戳，获得近似的传输时间戳。

SO_TIMESTAMPING
  在接收、传输或两者时生成时间戳。支持多个时间戳源，包括硬件。
  支持为流套接字生成时间戳。


1.1 SO_TIMESTAMP（也包括 SO_TIMESTAMP_OLD 和 SO_TIMESTAMP_NEW）
---------------------------------------------------------------

此套接字选项在接收路径上启用数据报的时间戳。由于目标套接字（如果有）
在网络栈早期未知，因此必须为所有数据包启用此功能。所有早期接收的时间
戳选项也是如此。

有关接口详细信息，请参阅 `man 7 socket`。

始终使用 SO_TIMESTAMP_NEW 时间戳以获得 struct __kernel_sock_timeval
格式的时间戳。

如果时间在 2038 年后，SO_TIMESTAMP_OLD 在 32 位机器上将返回错误的时间戳。

1.2 SO_TIMESTAMPNS（也包括 SO_TIMESTAMPNS_OLD 和 SO_TIMESTAMPNS_NEW）
---------------------------------------------------------------------

此选项与 SO_TIMESTAMP 相同，但返回数据类型有所不同。其 struct timespec
能达到比 SO_TIMESTAMP 的 timeval（毫秒）更高的分辨率（纳秒）时间戳。

始终使用 SO_TIMESTAMPNS_NEW 时间戳获得 struct __kernel_timespec 格式
的时间戳。

如果时间在 2038 年后，SO_TIMESTAMPNS_OLD 在 32 位机器上将返回错误的时间戳。

1.3 SO_TIMESTAMPING（也包括 SO_TIMESTAMPING_OLD 和 SO_TIMESTAMPING_NEW）
------------------------------------------------------------------------

支持多种类型的时间戳请求。因此，此套接字选项接受标志位图，而不是布尔值。在::

  err = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &val, sizeof(val));

val 是一个整数，设置了以下任何位。设置其他位将返回 EINVAL 且不更改当前状态。

这个套接字选项配置以下几个方面的时间戳生成：
为单个 sk_buff 结构体生成时间戳（1.3.1）；
将时间戳报告到套接字的错误队列（1.3.2）；
配置相关选项（1.3.3）；
也可以通过 cmsg 为单个 sendmsg 调用启用时间戳生成（1.3.4）。

1.3.1 时间戳生成
^^^^^^^^^^^^^^^^

某些位是向协议栈请求尝试生成时间戳。它们的任何组合都是有效的。对这些位的更改适
用于新创建的数据包，而不是已经在协议栈中的数据包。因此，可以通过在两个 setsockopt
调用之间嵌入 send() 调用来选择性地为数据包子集请求时间戳（例如，用于采样），
一个用于启用时间戳生成，一个用于禁用它。时间戳也可能由于特定套接字请求之外的原
因而生成，例如在当系统范围内启用接收时间戳时，如前所述。

SOF_TIMESTAMPING_RX_HARDWARE:
  请求由网络适配器生成的接收时间戳。

SOF_TIMESTAMPING_RX_SOFTWARE:
  当数据进入内核时请求接收时间戳。这些时间戳在设备驱动程序将数据包交给内核接收
  协议栈后生成。

SOF_TIMESTAMPING_TX_HARDWARE:
  请求由网络适配器生成的传输时间戳。此标志可以通过套接字选项和控制消息启用。

SOF_TIMESTAMPING_TX_SOFTWARE:
  当数据离开内核时请求传输（TX）时间戳。这些时间戳由设备驱动程序生成，并且尽
  可能贴近网络接口发送点，但始终在内核将数据包传递给网络接口之前生成。因此，
  它们需要驱动程序支持，且可能并非所有设备都可用。此标志可通过套接字选项和
  控制消息两种方式启用。

SOF_TIMESTAMPING_TX_SCHED:
  在进入数据包调度器之前请求传输时间戳。内核传输延迟（如果很长）通常由排队
  延迟主导。此时间戳与在 SOF_TIMESTAMPING_TX_SOFTWARE 处获取的时间戳之
  间的差异将暴露此延迟，并且与协议处理无关。协议处理中产生的延迟（如果有）
  可以通过从 send() 之前立即获取的用户空间时间戳中减去此时间戳来计算。在
  具有虚拟设备的机器上，传输的数据包通过多个设备和多个数据包调度器，在每层
  生成时间戳。这允许对排队延迟进行细粒度测量。此标志可以通过套接字选项和控
  制消息启用。

SOF_TIMESTAMPING_TX_ACK:
  请求在发送缓冲区中的所有数据都已得到确认时生成传输（TX）时间戳。此选项
  仅适用于可靠协议，目前仅在TCP协议中实现。对于该协议，它可能会过度报告
  测量结果，因为时间戳是在send()调用时缓冲区中的所有数据（包括该缓冲区）
  都被确认时生成的，即累积确认。该机制会忽略选择确认（SACK）和前向确认
  （FACK）。此标志可通过套接字选项和控制消息两种方式启用。

SOF_TIMESTAMPING_TX_COMPLETION:
  在数据包传输完成时请求传输时间戳。完成时间戳由内核在从硬件接收数据包完成
  报告时生成。硬件可能一次报告多个数据包，完成时间戳反映报告的时序而不是实
  际传输时间。此标志可以通过套接字选项和控制消息启用。


1.3.2 时间戳报告
^^^^^^^^^^^^^^^^

其他三个位控制将在生成的控制消息中报告哪些时间戳。对这些位的更改在协议栈中
的时间戳报告位置立即生效。仅当数据包设置了相关的时间戳生成请求时，才会报告
其时间戳。

SOF_TIMESTAMPING_SOFTWARE:
  在可用时报告任何软件时间戳。

SOF_TIMESTAMPING_SYS_HARDWARE:
  此选项已被弃用和忽略。

SOF_TIMESTAMPING_RAW_HARDWARE:
  在可用时报告由 SOF_TIMESTAMPING_TX_HARDWARE 或 SOF_TIMESTAMPING_RX_HARDWARE
  生成的硬件时间戳。


1.3.3 时间戳选项
^^^^^^^^^^^^^^^^

接口支持以下选项

SOF_TIMESTAMPING_OPT_ID:
  每个数据包生成一个唯一标识符。一个进程可以同时存在多个未完成的时间戳请求。
  数据包在传输路径中可能会发生重排序（例如在数据包调度器中）。在这种情况下，
  时间戳会以与原始send()调用不同的顺序排队到错误队列中。如此一来，仅根据
  时间戳顺序或 payload（有效载荷）检查，并不总能将时间戳与原始send()调用
  唯一匹配。

  此选项在 send() 时将每个数据包与唯一标识符关联，并与时间戳一起返回。
  标识符源自每个套接字的 u32 计数器（会回绕）。对于数据报套接字，计数器
  随每个发送的数据包递增。对于流套接字，它随每个字节递增。对于流套接字，
  还要设置 SOF_TIMESTAMPING_OPT_ID_TCP，请参阅下面的部分。

  计数器从零开始。在首次启用套接字选项时初始化。在禁用后再重新启用选项时
  重置。重置计数器不会更改系统中现有数据包的标识符。

  此选项仅针对传输时间戳实现。在这种情况下，时间戳总是与sock_extended_err
  结构体一起回环。该选项会修改ee_data字段，以传递一个在该套接字所有同时
  存在的未完成时间戳请求中唯一的 ID。

  进程可以通过控制消息SCM_TS_OPT_ID（TCP 套接字不支持）传递特定 ID，
  从而选择性地覆盖默认生成的 ID，示例如下::

    struct msghdr *msg;
    ...
    cmsg			 = CMSG_FIRSTHDR(msg);
    cmsg->cmsg_level		 = SOL_SOCKET;
    cmsg->cmsg_type		 = SCM_TS_OPT_ID;
    cmsg->cmsg_len		 = CMSG_LEN(sizeof(__u32));
    *((__u32 *) CMSG_DATA(cmsg)) = opt_id;
    err = sendmsg(fd, msg, 0);


SOF_TIMESTAMPING_OPT_ID_TCP:
  与 SOF_TIMESTAMPING_OPT_ID 一起传递给新的 TCP 时间戳应用程序。
  SOF_TIMESTAMPING_OPT_ID 定义了流套接字计数器的增量，但其起始点
  并不完全显而易见。此选项修复了这一点。

  对于流套接字，如果设置了 SOF_TIMESTAMPING_OPT_ID，则此选项应始终
  设置。在数据报套接字上，选项没有效果。

  一个合理的期望是系统调用后计数器重置为零，因此后续写入 N 字节将生成
  计数器为 N-1 的时间戳。SOF_TIMESTAMPING_OPT_ID_TCP 在所有条件下
  都实现了此行为。

  SOF_TIMESTAMPING_OPT_ID 不带修饰符时通常报告相同，特别是在套接字选项
  在无数据传输时设置时。如果正在传输数据，它可能与输出队列的长度（SIOCOUTQ）
  偏差。

  差异是由于基于 snd_una 与 write_seq 的。snd_una 是 peer 确认的 stream
  的偏移量。这取决于外部因素，例如网络 RTT。write_seq 是进程写入的最后一个
  字节。此偏移量不受外部输入影响。

  差异细微，在套接字选项初始化时配置时不易察觉，但 SOF_TIMESTAMPING_OPT_ID_TCP
  行为在任何时候都更稳健。

SOF_TIMESTAMPING_OPT_CMSG:
  支持所有时间戳数据包的 recv() cmsg。控制消息已无条件地在所有接收时间戳数据包
  和 IPv6 数据包上支持，以及在发送时间戳数据包的 IPv4 数据包上支持。此选项扩展
  了它们以在发送时间戳数据包的 IPv4 数据包上支持。一个用例是启用 socket 选项
  IP_PKTINFO 以关联数据包与其出口设备，通过启用 socket 选项 IP_PKTINFO 同时。


SOF_TIMESTAMPING_OPT_TSONLY:
  仅适用于传输时间戳。使内核返回一个 cmsg 与一个空数据包一起，而不是与原
  始数据包一起。这减少了套接字接收预算（SO_RCVBUF）中收取的内存量，并即使
  在 sysctl net.core.tstamp_allow_data 为 0 时也提供时间戳。此选项禁用
  SOF_TIMESTAMPING_OPT_CMSG。

SOF_TIMESTAMPING_OPT_STATS:
  与传输时间戳一起获取的选项性统计信息。它必须与 SOF_TIMESTAMPING_OPT_TSONLY
  一起使用。当传输时间戳可用时，统计信息可在类型为 SCM_TIMESTAMPING_OPT_STATS
  的单独控制消息中获取，作为 TLV（struct nlattr）类型的列表。这些统计信息允许应
  用程序将各种传输层统计信息与传输时间戳关联，例如某个数据块被 peer 的接收窗口限
  制了多长时间。

SOF_TIMESTAMPING_OPT_PKTINFO:
  启用 SCM_TIMESTAMPING_PKTINFO 控制消息以接收带有硬件时间戳的数据包。
  消息包含 struct scm_ts_pktinfo，它提供接收数据包的实际接口索引和层 2 长度。
  只有在 CONFIG_NET_RX_BUSY_POLL 启用且驱动程序使用 NAPI 时，才会返回非零的
  有效接口索引。该结构还包含另外两个字段，但它们是保留字段且未定义。

SOF_TIMESTAMPING_OPT_TX_SWHW:
  请求在 SOF_TIMESTAMPING_TX_HARDWARE 和 SOF_TIMESTAMPING_TX_SOFTWARE
  同时启用时，为传出数据包生成硬件和软件时间戳。如果同时生成两个时间戳，两个单
  独的消息将回环到套接字的错误队列，每个消息仅包含一个时间戳。

SOF_TIMESTAMPING_OPT_RX_FILTER:
  过滤掉虚假接收时间戳：仅当匹配的时间戳生成标志已启用时才报告接收时间戳。

  接收时间戳在入口路径中生成较早，在数据包的目的套接字确定之前。如果任何套接
  字启用接收时间戳，所有套接字的数据包将接收时间戳数据包。包括那些请求时间戳
  报告与 SOF_TIMESTAMPING_SOFTWARE 和/或 SOF_TIMESTAMPING_RAW_HARDWARE，
  但未请求接收时间戳生成。这可能发生在仅请求发送时间戳时。

  接收虚假时间戳通常是无害的。进程可以忽略意外的非零值。但它使行为在其他套接
  字上微妙地依赖。此标志隔离套接字以获得更确定的行为。

新应用程序鼓励传递 SOF_TIMESTAMPING_OPT_ID 以区分时间戳并传递
SOF_TIMESTAMPING_OPT_TSONLY 以操作，而不管 sysctl net.core.tstamp_allow_data
的设置。

例外情况是当进程需要额外的 cmsg 数据时，例如 SOL_IP/IP_PKTINFO 以检测出
口网络接口。然后传递选项 SOF_TIMESTAMPING_OPT_CMSG。此选项依赖于访问原
始数据包的内容，因此不能与 SOF_TIMESTAMPING_OPT_TSONLY 组合。


1.3.4. 通过控制消息启用时间戳
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

除了套接字选项外，时间戳生成还可以通过 cmsg 按写入请求，仅适用于
SOF_TIMESTAMPING_TX_*（见第 1.3.1 节）。使用此功能，应用程序可以无需启用和
禁用时间戳即可采样每个 sendmsg() 的时间戳::

  struct msghdr *msg;
  ...
  cmsg			       = CMSG_FIRSTHDR(msg);
  cmsg->cmsg_level	       = SOL_SOCKET;
  cmsg->cmsg_type	       = SO_TIMESTAMPING;
  cmsg->cmsg_len	       = CMSG_LEN(sizeof(__u32));
  *((__u32 *) CMSG_DATA(cmsg)) = SOF_TIMESTAMPING_TX_SCHED |
				 SOF_TIMESTAMPING_TX_SOFTWARE |
				 SOF_TIMESTAMPING_TX_ACK;
  err = sendmsg(fd, msg, 0);

通过 cmsg 设置的 SOF_TIMESTAMPING_TX_* 标志将覆盖通过 setsockopt 设置的
SOF_TIMESTAMPING_TX_* 标志。

此外，应用程序仍然需要通过 setsockopt 启用时间戳报告以接收时间戳::

  __u32 val = SOF_TIMESTAMPING_SOFTWARE |
	      SOF_TIMESTAMPING_OPT_ID /* 或任何其他标志 */;
  err = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &val, sizeof(val));


1.4 字节流时间戳
----------------

SO_TIMESTAMPING 接口支持字节流的时间戳。每个请求解释为请求当整个缓冲区内容
通过时间戳点时。也就是说，对于流选项 SOF_TIMESTAMPING_TX_SOFTWARE 将记录
当所有字节都到达设备驱动程序时，无论数据被转换成多少个数据包。

一般来说，字节流没有自然分隔符，因此将时间戳与数据相关联是非平凡的。字节范围
可能跨段，任何段可能合并（可能合并先前分段缓冲区关联的独立 send() 调用）。段
可以重新排序，同一字节范围可以在多个段中并存，对于实现重传的协议。

所有时间戳必须实现相同的语义，否则它们是不可比较的。以不同于简单情况（缓冲区
到 skb 的 1:1 映射）的方式处理“罕见”角落情况是不够的，因为性能调试通常需要
关注这些异常。

在实践中，时间戳可以与字节流段一致地关联，如果时间戳语义和测量时序的选择正确。
此挑战与决定 IP 分片策略没有不同。在那里，定义是仅对第一个分片进行时间戳。对
于字节流，我们选择仅在所有字节通过某个点时生成时间戳。SOF_TIMESTAMPING_TX_ACK
定义的实现和推理是容易的。一个需要考虑 SACK 的实现会更复杂，因为可能存在传输
空洞和乱序到达。

在主机上，TCP 也可以通过 Nagle、cork、autocork、分段和 GSO 打破简单的 1:1
缓冲区到 skbuff 映射。实现确保在所有情况下都正确，通过跟踪每个 send() 传递
给send() 的最后一个字节，即使它在 skbuff 扩展或合并操作后不再是最后一个字
节。它存储相关的序列号在 skb_shinfo(skb)->tskey。因为一个 skbuff 只有一
个这样的字段，所以只能生成一个时间戳。

在罕见情况下，如果两个请求折叠到同一个 skb，则时间戳请求可能会被错过。进程可
以通过始终在请求之间刷新 TCP 栈来检测此情况，例如启用 TCP_NODELAY 和禁用
TCP_CORK和 autocork。在 linux-4.7 之后，更好的预防合并方法是使用 MSG_EOR
标志在sendmsg()时。

这些预防措施确保时间戳仅在所有字节通过时间戳点时生成，假设网络栈本身不会重新
排序段。栈确实试图避免重新排序。唯一的例外是管理员控制：可以构造一个数据包调
度器配置，将来自同一流的不同段延迟不同。这种设置通常不常见。


2 数据接口
==========

时间戳通过 recvmsg() 的辅助数据功能读取。请参阅 `man 3 cmsg` 了解此接口的
详细信息。套接字手册页面 (`man 7 socket`) 描述了如何检索SO_TIMESTAMP 和
SO_TIMESTAMPNS 生成的数据包时间戳。


2.1 SCM_TIMESTAMPING 记录
-------------------------

这些时间戳在 cmsg_level SOL_SOCKET、cmsg_type SCM_TIMESTAMPING 和类型为

对于 SO_TIMESTAMPING_OLD::

	struct scm_timestamping {
		struct timespec ts[3];
	};

对于 SO_TIMESTAMPING_NEW::

	struct scm_timestamping64 {
		struct __kernel_timespec ts[3];

始终使用 SO_TIMESTAMPING_NEW 时间戳以始终获得 struct scm_timestamping64
格式的时间戳。

SO_TIMESTAMPING_OLD 在 32 位机器上 2038 年后返回错误的时间戳。

该结构可以返回最多三个时间戳。这是一个遗留功能。任何时候至少有一个字
段不为零。大多数时间戳都通过 ts[0] 传递。硬件时间戳通过 ts[2] 传递。

ts[1] 以前用于存储硬件时间戳转换为系统时间。相反，将硬件时钟设备直接
暴露为HW PTP时钟源，以允许用户空间进行时间转换，并可选地与用户空间
PTP 堆栈（如linuxptp）同步系统时间。对于 PTP 时钟 API，请参阅
Documentation/driver-api/ptp.rst。

注意，如果同时启用了 SO_TIMESTAMP 或 SO_TIMESTAMPNS 与
SO_TIMESTAMPING 使用 SOF_TIMESTAMPING_SOFTWARE，在 recvmsg()
调用时会生成一个虚假的软件时间戳，并传递给 ts[0] 当真实软件时间戳缺
失时。这也发生在硬件传输时间戳上。

2.1.1 传输时间戳与 MSG_ERRQUEUE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

对于传输时间戳，传出数据包回环到套接字的错误队列，并附加发送时间戳（s）。
进程通过调用带有 MSG_ERRQUEUE 标志的 recvmsg() 接收时间戳，并传递
一个足够大的 msg_control缓冲区以接收相关的元数据结构。recvmsg 调用
返回原始传出数据包，并附加两个辅助消息。

一个 cm_level SOL_IP(V6) 和 cm_type IP(V6)_RECVERR 嵌入一个
struct sock_extended_err这定义了错误类型。对于时间戳，ee_errno
字段是 ENOMSG。另一个辅助消息将具有 cm_level SOL_SOCKET 和 cm_type
SCM_TIMESTAMPING。这嵌入了 struct scm_timestamping。


2.1.1.2 时间戳类型
~~~~~~~~~~~~~~~~~~

三个 struct timespec 的语义由 struct sock_extended_err 中的
ee_info 字段定义。它包含一个类型 SCM_TSTAMP_* 来定义实际传递给
scm_timestamping 的时间戳。

SCM_TSTAMP_* 类型与之前讨论的 SOF_TIMESTAMPING_* 控制字段完全
匹配，只有一个例外对于遗留原因，SCM_TSTAMP_SND 等于零，可以设置为
SOF_TIMESTAMPING_TX_HARDWARE 和 SOF_TIMESTAMPING_TX_SOFTWARE。
它是第一个，如果 ts[2] 不为零，否则是第二个，在这种情况下，时间戳存
储在ts[0] 中。


2.1.1.3 分片
~~~~~~~~~~~~

传出数据报分片很少见，但可能发生，例如通过显式禁用 PMTU 发现。如果
传出数据包被分片，则仅对第一个分片进行时间戳，并返回给发送套接字。


2.1.1.4 数据包负载
~~~~~~~~~~~~~~~~~~

调用应用程序通常不关心接收它传递给堆栈的整个数据包负载：套接字错误队
列机制仅是一种将时间戳附加到其上的方法。在这种情况下，应用程序可以选
择读取较小的数据报，甚至长度为 0。负载相应地被截断。直到进程调用
recvmsg() 到错误队列，然而，整个数据包仍在队列中，占用 SO_RCVBUF 预算。


2.1.1.5 阻塞读取
~~~~~~~~~~~~~~~~

从错误队列读取始终是非阻塞操作。要阻塞等待时间戳，请使用 poll 或
select。poll() 将在 pollfd.revents 中返回 POLLERR，如果错误队列
中有数据。没有必要在 pollfd.events中传递此标志。此标志在请求时被忽
略。另请参阅 `man 2 poll`。


2.1.2 接收时间戳
^^^^^^^^^^^^^^^^

在接收时，没有理由从套接字错误队列读取。SCM_TIMESTAMPING 辅助数据与
数据包数据一起通过正常 recvmsg() 发送。由于这不是套接字错误，它不伴
随消息 SOL_IP(V6)/IP(V6)_RECVERROR。在这种情况下，struct
scm_timestamping 中的三个字段含义隐式定义。ts[0] 在设置时包含软件
时间戳，ts[1] 再次被弃用，ts[2] 在设置时包含硬件时间戳。


3. 硬件时间戳配置：ETHTOOL_MSG_TSCONFIG_SET/GET
===============================================

硬件时间戳也必须为每个设备驱动程序初始化，该驱动程序预期执行硬件时间戳。
参数在 include/uapi/linux/net_tstamp.h 中定义为::

	struct hwtstamp_config {
		int flags;	/* 目前没有定义的标志，必须为零 */
		int tx_type;	/* HWTSTAMP_TX_* */
		int rx_filter;	/* HWTSTAMP_FILTER_* */
	};

期望的行为通过 tsconfig netlink 套接字 ``ETHTOOL_MSG_TSCONFIG_SET``
传递到内核，并通过 ``ETHTOOL_A_TSCONFIG_TX_TYPES``、
``ETHTOOL_A_TSCONFIG_RX_FILTERS`` 和 ``ETHTOOL_A_TSCONFIG_HWTSTAMP_FLAGS``
netlink 属性设置 struct hwtstamp_config 相应地。

``ETHTOOL_A_TSCONFIG_HWTSTAMP_PROVIDER`` netlink 嵌套属性用于选择
硬件时间戳的来源。它由设备源的索引和时间戳类型限定符组成。

驱动程序可以自由使用比请求更宽松的配置。预期驱动程序应仅实现可以直接支持的
最通用模式。例如，如果硬件可以支持 HWTSTAMP_FILTER_PTP_V2_EVENT，则它
通常应始终升级HWTSTAMP_FILTER_PTP_V2_L2_SYNC，依此类推，因为
HWTSTAMP_FILTER_PTP_V2_EVENT 更通用（更实用）。

支持硬件时间戳的驱动程序应更新 struct，并可能返回更宽松的实际配置。如果
请求的数据包无法进行时间戳，则不应更改任何内容，并返回 ERANGE（与 EINVAL
相反，这表明 SIOCSHWTSTAMP 根本不支持）。

只有具有管理权限的进程才能更改配置。用户空间负责确保多个进程不会相互干扰，
并确保设置被重置。

任何进程都可以通过请求 tsconfig netlink 套接字 ``ETHTOOL_MSG_TSCONFIG_GET``
读取实际配置。

遗留配置是使用 ioctl(SIOCSHWTSTAMP) 与指向 struct ifreq 的指针，其
ifr_data指向 struct hwtstamp_config。tx_type 和 rx_filter 是驱动
程序期望执行的提示。如果请求的细粒度过滤对传入数据包不支持，驱动程序可能
会对请求的数据包进行时间戳。ioctl(SIOCGHWTSTAMP) 以与
ioctl(SIOCSHWTSTAMP) 相同的方式使用。然而,并非所有驱动程序都实现了这一点。

::

    /* 可能的 hwtstamp_config->tx_type 值 */
    enum {
	    /*
	    * 不会需要硬件时间戳的传出数据包；
	    * 如果数据包到达并请求它，则不会进行硬件时间戳
	    */
	    HWTSTAMP_TX_OFF,

	    /*
	    * 启用传出数据包的硬件时间戳；
	    * 数据包的发送者决定哪些数据包需要时间戳，
	    * 在发送数据包之前设置 SOF_TIMESTAMPING_TX_SOFTWARE
	    */
	    HWTSTAMP_TX_ON,
    };

    /* 可能的 hwtstamp_config->rx_filter 值 */
    enum {
	    /* 时间戳不传入任何数据包 */
	    HWTSTAMP_FILTER_NONE,

	    /* 时间戳任何传入数据包 */
	    HWTSTAMP_FILTER_ALL,

	    /* 返回值：时间戳所有请求的数据包加上一些其他数据包 */
	    HWTSTAMP_FILTER_SOME,

	    /* PTP v1，UDP，任何事件数据包 */
	    HWTSTAMP_FILTER_PTP_V1_L4_EVENT,

	    /* 有关完整值列表，请检查
	    * 文件 include/uapi/linux/net_tstamp.h
	    */
    };

3.1 硬件时间戳实现：设备驱动程序
--------------------------------

支持硬件时间戳的驱动程序必须支持 ndo_hwtstamp_set NDO 或遗留 SIOCSHWTSTAMP
ioctl 并更新提供的 struct hwtstamp_config 与实际值，如 SIOCSHWTSTAMP 部分
所述。它还应支持 ndo_hwtstamp_get 或遗留 SIOCGHWTSTAMP。

接收数据包的时间戳必须存储在 skb 中。要获取 skb 的共享时间戳结构，请调用
skb_hwtstamps()。然后设置结构中的时间戳::

    struct skb_shared_hwtstamps {
	    /* 硬件时间戳转换为自任意时间点的持续时间
	    * 自定义点
	    */
	    ktime_t	hwtstamp;
    };

传出数据包的时间戳应按如下方式生成：

- 在 hard_start_xmit() 中，检查 (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
  是否不为零。如果是，则驱动程序期望执行硬件时间戳。
- 如果此 skb 和请求都可能，则声明驱动程序正在执行时间戳，通过设置 skb_shinfo(skb)->tx_flags
  中的标志SKBTX_IN_PROGRESS，例如::

      skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

  您可能希望保留与 skb 关联的指针，而不是释放 skb。不支持硬件时间戳的驱
  动程序不会这样做。驱动程序绝不能触及 sk_buff::tstamp！它用于存储网络
  子系统生成的软件时间戳。
- 驱动程序应在尽可能接近将 sk_buff 传递给硬件时调用 skb_tx_timestamp()。
  skb_tx_timestamp()提供软件时间戳（如果请求），并且硬件时间戳不可用
  （SKBTX_IN_PROGRESS 未设置）。
- 一旦驱动程序发送数据包并/或获取硬件时间戳，它就会通过 skb_tstamp_tx()
  传递时间戳，原始 skb，原始硬件时间戳。skb_tstamp_tx() 克隆原始 skb 并
  添加时间戳，因此原始 skb 现在必须释放。如果获取硬件时间戳失败，则驱动程序
  不应回退到软件时间戳。理由是，这会在处理管道中的稍后时间发生，而不是其他软
  件时间戳，因此可能导致时间戳之间的差异。

3.2 堆叠 PTP 硬件时钟的特殊考虑
-------------------------------

在数据包的路径中可能存在多个 PHC（PTP 硬件时钟）。内核没有明确的机制允许用
户选择用于时间戳以太网帧的 PHC。相反，假设最外层的 PHC 始终是最优的，并且
内核驱动程序协作以实现这一目标。目前有 3 种堆叠 PHC 的情况，如下所示：

3.2.1 DSA（分布式交换架构）交换机
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

这些是具有一个端口连接到（完全不知情的）主机以太网接口的以太网交换机，并且
执行端口多路复用或可选转发加速功能。每个 DSA 交换机端口在用户看来都是独立的
（虚拟）网络接口，其网络 I/O 在底层通过主机接口（在 TX 上重定向到主机端口，
在 RX 上拦截帧）执行。

当 DSA 交换机连接到主机端口时，PTP 同步必须受到限制，因为交换机的可变排队
延迟引入了主机端口与其 PTP 伙伴之间的路径延迟抖动。因此，一些 DSA 交换机
包含自己的时间戳时钟，并具有在自身 MAC上执行网络时间戳的能力，因此路径延迟
仅测量线缆和 PHY 传播延迟。支持 Linux 的 DSA 交换机暴露了与任何其他网络
接口相同的 ABI（除了 DSA 接口在网络 I/O 方面实际上是虚拟的，它们确实有自
己的PHC）。典型地，但不是强制性地，所有DSA 交换机接口共享相同的 PHC。

通过设计，DSA 交换机对连接到其主机端口的 PTP 时间戳不需要任何特殊的驱动程
序处理。然而，当主机端口也支持 PTP 时间戳时，DSA 将负责拦截
``.ndo_eth_ioctl`` 调用，并阻止尝试在主机端口上启用硬件时间戳。这是因为
SO_TIMESTAMPING API 不允许为同一数据包传递多个硬件时间戳，因此除了 DSA
交换机端口之外的任何人都不应阻止这样做。

在通用层，DSA 提供了以下基础设施用于 PTP 时间戳：

- ``.port_txtstamp()``：在用户空间从用户空间请求带有硬件 TX 时间戳请求
  的数据包之前调用的钩子。这是必需的，因为硬件时间戳在实际 MAC 传输后才可
  用，因此驱动程序必须准备将时间戳与原始数据包相关联，以便它可以重新入队数
  据包到套接字的错误队列。为了保存可能在时间戳可用时需要的数据包，驱动程序
  可以调用 ``skb_clone_sk``，在 skb->cb 中保存克隆指针，并入队一个 tx
  skb 队列。通常，交换机会有一个PTP TX 时间戳寄存器（或有时是一个 FIFO），
  其中时间戳可用。在 FIFO 的情况下，硬件可能会存储PTP 序列 ID/消息类型/
  域号和实际时间戳的键值对。为了在等待时间戳的数据包队列和实际时间戳之间正
  确关联，驱动程序可以使用 BPF 分类器(``ptp_classify_raw``) 来识别 PTP
  传输类型，并使用 ``ptp_parse_header`` 解释 PTP 头字段。可能存在一个 IRQ，
  当此时间戳可用时触发，或者驱动程序可能需要轮询，在调用 ``dev_queue_xmit()``
  到主机接口之后。单步 TX 时间戳不需要数据包克隆，因为 PTP 协议不需要后续消
  息（因为TX 时间戳已嵌入到数据包中），因此用户空间不期望数据包带有 TX 时间戳
  被重新入队到其套接字的错误队列。

- ``.port_rxtstamp()``：在 RX 上，DSA 运行 BPF 分类器以识别 PTP 事件消息
  （任何其他数据包，包括 PTP 通用消息，不进行时间戳）。驱动程序提供原始（也是唯一）
  时间戳数据包，以便它可以标记它，如果它是立即可用的，或者延迟。在接收时，时间
  戳可能要么在频带内（通过DSA 头中的元数据，或以其他方式附加到数据包），要么在频
  带外（通过另一个 RX 时间戳FIFO）。在 RX 上延迟通常是必要的，当检索时间戳需要
  可睡眠上下文时。在这种情况下，DSA驱动程序有责任调用 ``netif_rx()`` 在新鲜时
  间戳的 skb 上。

3.2.2 以太网 PHYs
^^^^^^^^^^^^^^^^^

这些是通常在网络栈中履行第 1 层角色的设备，因此它们在 DSA 交换机中没有网络接
口的表示。然而，PHY可能能够检测和时间戳 PTP 数据包，出于性能原因：在尽可能接
近导线的地方获取的时间戳具有更稳定的同步性和更精确的精度。

支持 PTP 时间戳的 PHY 驱动程序必须创建 ``struct mii_timestamper`` 并添加
指向它的指针在 ``phydev->mii_ts`` 中。 ``phydev->mii_ts`` 的存在将由网络
堆栈检查。

由于 PHY 没有网络接口表示，PHY 的时间戳和 ethtool ioctl 操作需要通过其各自
的 MAC驱动程序进行中介。因此，与 DSA 交换机不同，需要对每个单独的 MAC 驱动
程序进行 PHY时间戳支持的修改。这包括：

- 在 ``.ndo_eth_ioctl`` 中检查，是否 ``phy_has_hwtstamp(netdev->phydev)``
  为真或假。如果是，则 MAC 驱动程序不应处理此请求，而应将其传递给 PHY 使用
  ``phy_mii_ioctl()``。

- 在 RX 上，特殊干预可能或可能不需要，具体取决于将 skb 传递到网络堆栈的函数。
  在 plain ``netif_rx()`` 和类似情况下，MAC 驱动程序必须检查是否
  ``skb_defer_rx_timestamp(skb)`` 是必要的，如果是，则不调用 ``netif_rx()``。
  如果 ``CONFIG_NETWORK_PHY_TIMESTAMPING`` 启用，并且
  ``skb->dev->phydev->mii_ts`` 存在，它的 ``.rxtstamp()`` 钩子现在将被调
  用，以使用与 DSA 类似的逻辑确定 RX 时间戳延迟是否必要。同样像 DSA，它成为
  PHY 驱动程序的责任，在时间戳可用时发送数据包到堆栈。

  对于其他 skb 接收函数，例如 ``napi_gro_receive`` 和 ``netif_receive_skb``，
  堆栈会自动检查是否 ``skb_defer_rx_timestamp()`` 是必要的，因此此检查不
  需要在驱动程序内部。

- 在 TX 上，同样，特殊干预可能或可能不需要。调用 ``mii_ts->txtstamp()``钩
  子的函数名为``skb_clone_tx_timestamp()``。此函数可以直接调用（在这种情
  况下，确实需要显式 MAC 驱动程序支持），但函数也 piggybacks 从
  ``skb_tx_timestamp()`` 调用，许多 MAC 驱动程序已经为软件时间戳目的执行。
  因此，如果 MAC 支持软件时间戳，则它不需要在此阶段执行任何其他操作。

3.2.3 MII 总线嗅探设备
^^^^^^^^^^^^^^^^^^^^^^

这些执行与时间戳以太网 PHY 相同的角色，除了它们是离散设备，因此可以与任何 PHY
组合，即使它不支持时间戳。在 Linux 中，它们是可发现的，可以通过 Device Tree
附加到 ``struct phy_device``，对于其余部分，它们使用与那些相同的 mii_ts 基
础设施。请参阅 Documentation/devicetree/bindings/ptp/timestamper.txt 了
解更多详细信息。

3.2.4 MAC 驱动程序的其他注意事项
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

堆叠 PHC 可能会暴露 MAC 驱动程序的错误，这些错误在未堆叠 PHC 时无法触发。一个
例子涉及此行代码，已经在前面的部分中介绍过::

      skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

任何 TX 时间戳逻辑，无论是普通的 MAC 驱动程序、DSA 交换机驱动程序、PHY 驱动程
序还是 MII 总线嗅探设备驱动程序，都应该设置此标志。但一个未意识到 PHC 堆叠的
MAC 驱动程序可能会被其他不是它自己的实体设置此标志，并传递一个重复的时间戳。例
如，典型的 TX 时间戳逻辑可能是将传输部分分为 2 个部分：

1. "TX"：检查是否通过 ``.ndo_eth_ioctl``（"``priv->hwtstamp_tx_enabled
   == true``"）和当前 skb 是否需要 TX 时间戳（"``skb_shinfo(skb)->tx_flags
   & SKBTX_HW_TSTAMP``"）。如果为真，则设置 "``skb_shinfo(skb)->tx_flags
   |= SKBTX_IN_PROGRESS``" 标志。注意：如上所述，在堆叠 PHC 系统中，此条件
   不应触发，因为此 MAC 肯定不是最外层的 PHC。但这是典型的错误所在。传输继续
   使用此数据包。

2. "TX 确认"：传输完成。驱动程序检查是否需要收集任何 TX 时间戳。这里通常是典
   型的错误所在：驱动程序采取捷径，只检查 "``skb_shinfo(skb)->tx_flags &
   SKBTX_IN_PROGRESS``" 是否设置。在堆叠 PHC 系统中，这是错误的，因为此 MAC
   驱动程序不是唯一在 TX 数据路径中启用 SKBTX_IN_PROGRESS 的实体。

此问题的正确解决方案是 MAC 驱动程序在其 "TX 确认" 部分中有一个复合检查，不仅
针对 "``skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS``"，还针对
"``priv->hwtstamp_tx_enabled == true``"。因为系统确保 PTP 时间戳仅对最
外层 PHC 启用，此增强检查将避免向用户空间传递重复的 TX 时间戳。
