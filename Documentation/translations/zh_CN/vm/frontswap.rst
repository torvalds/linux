:Original: Documentation/vm/_free_page_reporting.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

=========
Frontswap
=========

Frontswap为交换页提供了一个 “transcendent memory” 的接口。在一些环境中，由
于交换页被保存在RAM（或类似RAM的设备）中，而不是交换磁盘，因此可以获得巨大的性能
节省（提高）。

.. _Transcendent memory in a nutshell: https://lwn.net/Articles/454795/

Frontswap之所以这么命名，是因为它可以被认为是与swap设备的“back”存储相反。存
储器被认为是一个同步并发安全的面向页面的“伪RAM设备”，符合transcendent memory
（如Xen的“tmem”，或内核内压缩内存，又称“zcache”，或未来的类似RAM的设备）的要
求；这个伪RAM设备不能被内核直接访问或寻址，其大小未知且可能随时间变化。驱动程序通过
调用frontswap_register_ops将自己与frontswap链接起来，以适当地设置frontswap_ops
的功能，它提供的功能必须符合某些策略，如下所示:

一个 “init” 将设备准备好接收与指定的交换设备编号（又称“类型”）相关的frontswap
交换页。一个 “store” 将把该页复制到transcendent memory，并与该页的类型和偏移
量相关联。一个 “load” 将把该页，如果找到的话，从transcendent memory复制到内核
内存，但不会从transcendent memory中删除该页。一个 “invalidate_page” 将从
transcendent memory中删除该页，一个 “invalidate_area” 将删除所有与交换类型
相关的页（例如，像swapoff）并通知 “device” 拒绝进一步存储该交换类型。

一旦一个页面被成功存储，在该页面上的匹配加载通常会成功。因此，当内核发现自己处于需
要交换页面的情况时，它首先尝试使用frontswap。如果存储的结果是成功的，那么数据就已
经成功的保存到了transcendent memory中，并且避免了磁盘写入，如果后来再读回数据，
也避免了磁盘读取。如果存储返回失败，transcendent memory已经拒绝了该数据，且该页
可以像往常一样被写入交换空间。

请注意，如果一个页面被存储，而该页面已经存在于transcendent memory中（一个 “重复”
的存储），要么存储成功，数据被覆盖，要么存储失败，该页面被废止。这确保了旧的数据永远
不会从frontswap中获得。

如果配置正确，对frontswap的监控是通过 `/sys/kernel/debug/frontswap` 目录下的
debugfs完成的。frontswap的有效性可以通过以下方式测量（在所有交换设备中）:

``failed_stores``
	有多少次存储的尝试是失败的

``loads``
	尝试了多少次加载（应该全部成功）

``succ_stores``
	有多少次存储的尝试是成功的

``invalidates``
	尝试了多少次作废

后台实现可以提供额外的指标。

经常问到的问题
==============

* 价值在哪里?

当一个工作负载开始交换时，性能就会下降。Frontswap通过提供一个干净的、动态的接口来
读取和写入交换页到 “transcendent memory”，从而大大增加了许多这样的工作负载的性
能，否则内核是无法直接寻址的。当数据被转换为不同的形式和大小（比如压缩）或者被秘密
移动（对于一些类似RAM的设备来说，这可能对写平衡很有用）时，这个接口是理想的。交换
页（和被驱逐的页面缓存页）是这种比RAM慢但比磁盘快得多的“伪RAM设备”的一大用途。

Frontswap对内核的影响相当小，为各种系统配置中更动态、更灵活的RAM利用提供了巨大的
灵活性：

在单一内核的情况下，又称“zcache”，页面被压缩并存储在本地内存中，从而增加了可以安
全保存在RAM中的匿名页面总数。Zcache本质上是用压缩/解压缩的CPU周期换取更好的内存利
用率。Benchmarks测试显示，当内存压力较低时，几乎没有影响，而在高内存压力下的一些
工作负载上，则有明显的性能改善（25%以上）。

“RAMster” 在zcache的基础上增加了对集群系统的 “peer-to-peer” transcendent memory
的支持。Frontswap页面像zcache一样被本地压缩，但随后被“remotified” 到另一个系
统的RAM。这使得RAM可以根据需要动态地来回负载平衡，也就是说，当系统A超载时，它可以
交换到系统B，反之亦然。RAMster也可以被配置成一个内存服务器，因此集群中的许多服务器
可以根据需要动态地交换到配置有大量内存的单一服务器上......而不需要预先配置每个客户
有多少内存可用

在虚拟情况下，虚拟化的全部意义在于统计地将物理资源在多个虚拟机的不同需求之间进行复
用。对于RAM来说，这真的很难做到，而且在不改变内核的情况下，要做好这一点的努力基本上
是失败的（除了一些广为人知的特殊情况下的工作负载）。具体来说，Xen Transcendent Memory
后端允许管理器拥有的RAM “fallow”，不仅可以在多个虚拟机之间进行“time-shared”，
而且页面可以被压缩和重复利用，以优化RAM的利用率。当客户操作系统被诱导交出未充分利用
的RAM时（如 “selfballooning”），突然出现的意外内存压力可能会导致交换；frontswap
允许这些页面被交换到管理器RAM中或从管理器RAM中交换（如果整体主机系统内存条件允许），
从而减轻计划外交换可能带来的可怕的性能影响。

一个KVM的实现正在进行中，并且已经被RFC'ed到lkml。而且，利用frontswap，对NVM作为
内存扩展技术的调查也在进行中。

* 当然，在某些情况下可能有性能上的优势，但frontswap的空间/时间开销是多少？

如果 CONFIG_FRONTSWAP 被禁用，每个 frontswap 钩子都会编译成空，唯一的开销是每
个 swapon'ed swap 设备的几个额外字节。如果 CONFIG_FRONTSWAP 被启用，但没有
frontswap的 “backend” 寄存器，每读或写一个交换页就会有一个额外的全局变量，而不
是零。如果 CONFIG_FRONTSWAP 被启用，并且有一个frontswap的backend寄存器，并且
后端每次 “store” 请求都失败（即尽管声称可能，但没有提供内存），CPU 的开销仍然可以
忽略不计 - 因为每次frontswap失败都是在交换页写到磁盘之前，系统很可能是 I/O 绑定
的，无论如何使用一小部分的 CPU 都是不相关的。

至于空间，如果CONFIG_FRONTSWAP被启用，并且有一个frontswap的backend注册，那么
每个交换设备的每个交换页都会被分配一个比特。这是在内核已经为每个交换设备的每个交换
页分配的8位（在2.6.34之前是16位）上增加的。(Hugh Dickins观察到，frontswap可能
会偷取现有的8个比特，但是我们以后再来担心这个小的优化问题)。对于标准的4K页面大小的
非常大的交换盘（这很罕见），这是每32GB交换盘1MB开销。

当交换页存储在transcendent memory中而不是写到磁盘上时，有一个副作用，即这可能会
产生更多的内存压力，有可能超过其他的优点。一个backend，比如zcache，必须实现策略
来仔细（但动态地）管理内存限制，以确保这种情况不会发生。

* 好吧，那就用内核骇客能理解的术语来快速概述一下这个frontswap补丁的作用如何？

我们假设在内核初始化过程中，一个frontswap 的 “backend” 已经注册了；这个注册表
明这个frontswap 的 “backend” 可以访问一些不被内核直接访问的“内存”。它到底提
供了多少内存是完全动态和随机的。

每当一个交换设备被交换时，就会调用frontswap_init()，把交换设备的编号（又称“类
型”）作为一个参数传给它。这就通知了frontswap，以期待 “store” 与该号码相关的交
换页的尝试。

每当交换子系统准备将一个页面写入交换设备时（参见swap_writepage()），就会调用
frontswap_store。Frontswap与frontswap backend协商，如果backend说它没有空
间，frontswap_store返回-1，内核就会照常把页换到交换设备上。注意，来自frontswap
backend的响应对内核来说是不可预测的；它可能选择从不接受一个页面，可能接受每九个
页面，也可能接受每一个页面。但是如果backend确实接受了一个页面，那么这个页面的数
据已经被复制并与类型和偏移量相关联了，而且backend保证了数据的持久性。在这种情况
下，frontswap在交换设备的“frontswap_map” 中设置了一个位，对应于交换设备上的
页面偏移量，否则它就会将数据写入该设备。

当交换子系统需要交换一个页面时（swap_readpage()），它首先调用frontswap_load()，
检查frontswap_map，看这个页面是否早先被frontswap backend接受。如果是，该页
的数据就会从frontswap后端填充，换入就完成了。如果不是，正常的交换代码将被执行，
以便从真正的交换设备上获得这一页的数据。

所以每次frontswap backend接受一个页面时，交换设备的读取和（可能）交换设备的写
入都被 “frontswap backend store” 和（可能）“frontswap backend loads”
所取代，这可能会快得多。

* frontswap不能被配置为一个 “特殊的” 交换设备，它的优先级要高于任何真正的交换
  设备（例如像zswap，或者可能是swap-over-nbd/NFS）？

首先，现有的交换子系统不允许有任何种类的交换层次结构。也许它可以被重写以适应层次
结构，但这将需要相当大的改变。即使它被重写，现有的交换子系统也使用了块I/O层，它
假定交换设备是固定大小的，其中的任何页面都是可线性寻址的。Frontswap几乎没有触
及现有的交换子系统，而是围绕着块I/O子系统的限制，提供了大量的灵活性和动态性。

例如，frontswap backend对任何交换页的接受是完全不可预测的。这对frontswap backend
的定义至关重要，因为它赋予了backend完全动态的决定权。在zcache中，人们无法预
先知道一个页面的可压缩性如何。可压缩性 “差” 的页面会被拒绝，而 “差” 本身也可
以根据当前的内存限制动态地定义。

此外，frontswap是完全同步的，而真正的交换设备，根据定义，是异步的，并且使用
块I/O。块I/O层不仅是不必要的，而且可能进行 “优化”，这对面向RAM的设备来说是
不合适的，包括将一些页面的写入延迟相当长的时间。同步是必须的，以确保后端的动
态性，并避免棘手的竞争条件，这将不必要地大大增加frontswap和/或块I/O子系统的
复杂性。也就是说，只有最初的 “store” 和 “load” 操作是需要同步的。一个独立
的异步线程可以自由地操作由frontswap存储的页面。例如，RAMster中的 “remotification”
线程使用标准的异步内核套接字，将压缩的frontswap页面移动到远程机器。同样，
KVM的客户方实现可以进行客户内压缩，并使用 “batched” hypercalls。

在虚拟化环境中，动态性允许管理程序（或主机操作系统）做“intelligent overcommit”。
例如，它可以选择只接受页面，直到主机交换可能即将发生，然后强迫客户机做他们
自己的交换。

transcendent memory规格的frontswap有一个坏处。因为任何 “store” 都可
能失败，所以必须在一个真正的交换设备上有一个真正的插槽来交换页面。因此，
frontswap必须作为每个交换设备的 “影子” 来实现，它有可能容纳交换设备可能
容纳的每一个页面，也有可能根本不容纳任何页面。这意味着frontswap不能包含比
swap设备总数更多的页面。例如，如果在某些安装上没有配置交换设备，frontswap
就没有用。无交换设备的便携式设备仍然可以使用frontswap，但是这种设备的
backend必须配置某种 “ghost” 交换设备，并确保它永远不会被使用。


* 为什么会有这种关于 “重复存储” 的奇怪定义？如果一个页面以前被成功地存储过，
  难道它不能总是被成功地覆盖吗？

几乎总是可以的，不，有时不能。考虑一个例子，数据被压缩了，原来的4K页面被压
缩到了1K。现在，有人试图用不可压缩的数据覆盖该页，因此会占用整个4K。但是
backend没有更多的空间了。在这种情况下，这个存储必须被拒绝。每当frontswap
拒绝一个会覆盖的存储时，它也必须使旧的数据作废，并确保它不再被访问。因为交
换子系统会把新的数据写到读交换设备上，这是确保一致性的正确做法。

* 为什么frontswap补丁会创建新的头文件swapfile.h？

frontswap代码依赖于一些swap子系统内部的数据结构，这些数据结构多年来一直
在静态和全局之间来回移动。这似乎是一个合理的妥协：将它们定义为全局，但在一
个新的包含文件中声明它们，该文件不被包含swap.h的大量源文件所包含。

Dan Magenheimer，最后更新于2012年4月9日
