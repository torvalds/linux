.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scsi/scsi_mid_low_api.rst

:翻译:

 郝栋栋 doubled <doubled@leap-io-kernel.com>

:校译:



=========================
SCSI中间层 — 底层驱动接口
=========================

简介
====
本文档概述了Linux SCSI中间层与SCSI底层驱动之间的接口。底层
驱动（LLD）通常被称为主机总线适配器（HBA）驱动或主机驱动
（HD）。在该上下文中，“主机”指的是计算机IO总线（例如：PCI总
线或ISA总线）与SCSI传输层中单个SCSI启动器端口之间的桥梁。
“启动器”端口（SCSI术语，参考SAM-3：http://www.t10.org）向
“目标”SCSI端口（例如：磁盘）发送SCSI命令。在一个运行的系统
中存在多种底层驱动（LLDs），但每种硬件类型仅对应一种底层驱动
（LLD）。大多数底层驱动可以控制一个或多个SCSI HBA。部分HBA
内部集成多个主机控制器。

在某些情况下，SCSI传输层本身是已存在于Linux中的外部总线子系
统（例如：USB和ieee1394）。在此类场景下，SCSI子系统的底层驱
动将作为与其他驱动子系统的软件桥接层。典型示例包括
usb-storage驱动（位于drivers/usb/storage目录）以
及ieee1394/sbp2驱动（位于 drivers/ieee1394 目录）。

例如，aic7xxx底层驱动负责控制基于Adaptec公司7xxx芯片系列的
SCSI并行接口（SPI）控制器。aic7xxx底层驱动可以内建到内核中
或作为模块加载。一个Linux系统中只能运行一个aic7xxx底层驱动
程序，但他可能控制多个主机总线适配器（HBA）。这些HBA可能位于
PCI扩展卡或内置于主板中（或两者兼有）。某些基于aic7xxx的HBA
采用双控制器设计，因此会呈现为两个SCSI主机适配器。与大多数现
代HBA相同，每个aic7xxx控制器都拥有其独立的PCI设备地址。[SCSI
主机与PCI设备之间一一对应虽然常见，但并非强制要求（例如ISA适
配器就不适用此规则）。]

SCSI中间层将SCSI底层驱动（LLD）与其他层（例如SCSI上层驱动以
及块层）隔离开来。

本文档的版本大致与Linux内核2.6.8相匹配。

文档
====
内核源码树中设有专用的SCSI文档目录，通常位于
Documentation/scsi目录下。大多数文档采用
reStructuredText格式。本文档名为
scsi_mid_low_api.rst，可在该目录中找到。该文档的最新版本可
以访问 https://docs.kernel.org/scsi/scsi_mid_low_api.html
查阅。许多底层驱动（LLD）的文档也位于Documentation/scsi目录
下（例如aic7xxx.rst）。SCSI中间层的简要说明见scsi.rst文件，
该文档包含指向Linux Kernel 2.4系列SCSI子系统的文档链接。此
外还收录了两份SCSI上层驱动文档：st.rst（SCSI磁带驱动）与
scsi-generic.rst（用通用SCSI（sg）驱动）。

部分底层驱动的文档（或相关URL）可能嵌在C源代码文件或与其
源码同位于同一目录下。例如，USB大容量存储驱动的文档链接可以在
目录/usr/src/linux/drivers/usb/storage下找到。

驱动程序结构
============
传统上，SCSI子系统的底层驱动（LLD）至少包含drivers/scsi
目录下的两个文件。例如，一个名为“xyz”的驱动会包含一个头文件
xyz.h和一个源文件xyz.c。[实际上所有代码完全可以合并为单个
文件，头文件并非必需的。] 部分需要跨操作系统移植的底层驱动会
采用更复杂的文件结构。例如，aic7xxx驱动，就为通用代码与操作
系统专用代码（如FreeBSD和Linux）分别创建了独立的文件。此类
驱动通常会在drivers/scsi目录下拥有自己单独的子目录。

当需要向Linux内核添加新的底层驱动（LLD）时，必须留意
drivers/scsi目录下的两个文件：Makefile以及Kconfig。建议参
考现有底层驱动的代码组织方式。

随着Linux内核2.5开发内核逐步演进为2.6系列的生产版本，该接口
也引入了一些变化。以驱动初始化代码为例，现有两种模型可用。其
中旧模型与Linux内核2.4的实现相似，他基于在加载HBA驱动时检测
到的主机，被称为“被动（passive）”初始化模型。而新的模型允许
在底层驱动（LLD）的生命周期内动态拔插HBA，这种方式被称为“热
插拔（hotplug）”初始化模型。推荐使用新的模型，因为他既能处理
传统的永久连接SCSI设备，也能处理现代支持热插拔的类SCSI设备
（例如通过USB或IEEE 1394连接的数码相机）。这两种初始化模型将
在后续的章节中分别讨论。

SCSI底层驱动（LLD）通过以下3种方式与SCSI子系统进行交互：

  a) 直接调用由SCSI中间层提供的接口函数
  b) 将一组函数指针传递给中间层提供的注册函数，中间层将在
     后续运行的某个时刻调用这些函数。这些函数由LLD实现。
  c) 直接访问中间层维护的核心数据结构

a）组中所涉及的所有函数，均列于下文“中间层提供的函数”章节中。

b）组中涉及的所有函数均列于下文名为“接口函数”的章节中。这些
函数指针位于结构体struct scsi_host_template中，该结构体实
例会被传递给scsi_host_alloc()。对于LLD未实现的接口函数，应
对struct scsi_host_template中的对应成员赋NULL。如果在文件
作用域定义一个struct scsi_host_template的实例，没有显式初
始化的函数指针成员将自动设置为NULL。

c）组中提到的用法在“热插拔”环境中尤其需要谨慎处理。LLD必须
明确知晓这些与中间层及其他层级共享的数据结构的生命周期。

LLD中定义的所有函数以及在文件作用域内定义的所有数据都应声明
为static。例如，在一个名为“xxx”的LLD中的sdev_init()函数定
义如下：
``static int xxx_sdev_init(struct scsi_device * sdev) { /* code */ }``

热插拔初始化模型
================
在该模型中，底层驱动（LLD）控制SCSI主机适配器在子系统中的注
册与注销时机。主机最早可以在驱动初始化阶段被注册，最晚可以在
驱动卸载时被移除。通常，驱动会响应来自sysfs probe()的回调，
表示已检测到一个主机总线适配器（HBA）。在确认该新设备是LLD的
目标设备后，LLD初始化HBA，并将一个新的SCSI主机适配器注册到
SCSI中间层。

在LLD初始化过程中，驱动应当向其期望发现HBA的IO总线（例如PCI
总线）进行注册。该操作通常可以通过sysfs完成。任何驱动参数（
特别是那些在驱动加载后仍可修改的参数）也可以在此时通过sysfs
注册。当LLD注册其首个HBA时，SCSI中间层首次感受到该LLD的存在。

在稍后的某个时间点，当LLD检测到新的HBA时，接下来在LLD与SCSI
中间层之间会发生一系列典型的调用过程。该示例展示了中间层如何
扫描新引入的HBA，在该过程中发现了3个SCSI设备，其中只有前两个
设备有响应::

	HBA探测：假设在扫描中发现2个SCSI设备
    底层驱动               中间层               底层驱动
    =======---------------======---------------=======
    scsi_host_alloc()  -->
    scsi_add_host()  ---->
    scsi_scan_host()  -------+
			    |
			sdev_init()
			sdev_configure() -->  scsi_change_queue_depth()
			    |
			sdev_init()
			sdev_configure()
			    |
			sdev_init()   ***
			sdev_destroy() ***


    *** 对于SCSI中间层尝试扫描但未响应的SCSI设备，系统调用
	sdev_init()和sdev_destroy()函数对。

如果LLD期望调整默认队列设置，可以在其sdev_configure()例程
中调用scsi_change_queue_depth()。

当移除一个HBA时，可能是由于卸载LLD模块相关的有序关闭（例如通
过rmmod命令），也可能是由于sysfs的remove()回调而触发的“热拔
插”事件。无论哪种情况，其执行顺序都是相同的::

	    HBA移除：假设连接了2个SCSI设备
    底层驱动                     中间层                 底层驱动
    =======---------------------======-----------------=======
    scsi_remove_host() ---------+
				|
			sdev_destroy()
			sdev_destroy()
    scsi_host_put()

LLD用于跟踪struct Scsi_Host的实例可能会非常有用
（scsi_host_alloc()返回的指针）。这些实例由中间层“拥有”。
当引用计数为零时，struct Scsi_Host实例会被
scsi_host_put()释放。

HBA的热插拔是一个特殊的场景，特别是当HBA下的磁盘正在处理已挂
载文件系统上的SCSI命令时。为了应对其中的诸多问题，中间层引入
了引用计数逻辑。具体内容参考下文关于引用计数的章节。

热插拔概念同样适用于SCSI设备。目前，当添加HBA时，
scsi_scan_host() 函数会扫描该HBA所属SCSI传输通道上的设备。在
新型SCSI传输协议中，HBA可能在扫描完成后才检测到新的SCSI设备。
LLD可通过以下步骤通知中间层新SCSI设备的存在::

		    SCSI设备热插拔
    底层驱动                   中间层                 底层驱动
    =======-------------------======-----------------=======
    scsi_add_device()  ------+
			    |
			sdev_init()
			sdev_configure()   [--> scsi_change_queue_depth()]

类似的，LLD可能会感知到某个SCSI设备已经被移除（拔出）或与他的连
接已中断。某些现有的SCSI传输协议（例如SPI）可能直到后续SCSI命令
执行失败时才会发现设备已经被移除，中间层会将该设备设置为离线状态。
若LLD检测到SCSI设备已经被移除，可通过以下流程触发上层对该设备的
移除操作::

		    SCSI设备热拔插
    底层驱动                   中间层                 底层驱动
    =======-------------------======-----------------=======
    scsi_remove_device() -------+
				|
			sdev_destroy()

对于LLD而言，跟踪struct scsi_device实例可能会非常有用（该结构
的指针会作为参数传递给sdev_init()和sdev_configure()回调函数）。
这些实例的所有权归属于中间层（mid-level）。struct scsi_device
实例在sdev_destroy()执行后释放。

引用计数
========
Scsi_Host结构体已引入引用计数机制。该机制将struct Scsi_Host
实例的所有权分散到使用他的各SCSI层，而此前这类实例完全由中间
层独占管理。底层驱动（LLD）通常无需直接操作这些引用计数，仅在
某些特定场景下可能需要介入。

与struct Scsi_Host相关的引用计数函数主要有以下3种：

  - scsi_host_alloc():
	返回指向新实例的指针，该实例的引用计数被设置为1。

  - scsi_host_get():
	给定实例的引用计数加1。

  - scsi_host_put():
	给定实例的引用计数减1。如果引用计数减少到0，则释放该实例。

scsi_device结构体现已引入引用计数机制。该机制将
struct scsi_device实例的所有权分散到使用他的各SCSI层，而此
前这类实例完全由中间层独占管理。相关访问函数声明详见
include/scsi/scsi_device.h文件末尾部分。若LLD需要保留
scsi_device实例的指针副本，则应调用scsi_device_get()增加其
引用计数；不再需要该指针时，可通过scsi_device_put()递减引用
计数（该操作可能会导致该实例被释放）。

.. Note::

	struct Scsi_Host实际上包含两个并行维护的引用计数器，该引
	用计数由这些函数共同操作。

编码规范
========

首先，Linus Torvalds关于C语言编码风格的观点可以在
Documentation/process/coding-style.rst文件中找到。

此外，在相关gcc编译器支持的前提下，鼓励使用大多数C99标准的增强
特性。因此，在适当的情况下鼓励使用C99风格的结构体和数组初始化
方式。但不要过度使用，目前对可变长度数组（VLA）的支持还待完善。
一个例外是 ``//`` 风格的注释；在Linux中倾向于使
用 ``/*...*/`` 注释格式。

对于编写良好、经过充分测试且有完整文档的代码不需要重新格式化
以符合上述规范。例如，aic7xxx驱动是从FreeBSD和Adaptec代码库
移植到Linux的。毫无疑问，FreeBSD和Adaptec遵循其原有的编码规
范。


中间层提供的函数
================
这些函数由SCSI中间层提供，供底层驱动（LLD）调用。这些函数的名
称（即入口点）均已导出，因此作为模块加载的LLD可以访问他们。内
核会确保在任何LLD初始化之前，SCSI中间层已先行加载并完成初始化。
下文按字母顺序列出这些函数，其名称均以 ``scsi_`` 开头。

摘要：

  - scsi_add_device - 创建新的SCSI逻辑单元（LU）设备实例
  - scsi_add_host - 执行sysfs注册并设置传输类
  - scsi_change_queue_depth - 调整SCSI设备队列深度
  - scsi_bios_ptable - 返回块设备分区表的副本
  - scsi_block_requests - 阻止向指定主机提交新命令
  - scsi_host_alloc - 分配引用计数为1的新SCSI主机适配器实例scsi_host
  - scsi_host_get - 增加SCSI主机适配器实例的引用计数
  - scsi_host_put - 减少SCSI主机适配器的引用计数（归零时释放）
  - scsi_remove_device - 卸载并移除SCSI设备
  - scsi_remove_host - 卸载并移除主机控制器下的所有SCSI设备
  - scsi_report_bus_reset - 报告检测到的SCSI总线复位事件
  - scsi_scan_host - 执行SCSI总线扫描
  - scsi_track_queue_full - 跟踪连续出现的队列满事件
  - scsi_unblock_requests - 恢复向指定主机提交命令

详细信息::

    /**
    * scsi_add_device - 创建新的SCSI逻辑单元（LU）设备实例
    * @shost:   指向SCSI主机适配器实例的指针
    * @channel: 通道号（通常为0）
    * @id:      目标ID号
    * @lun:     逻辑单元号（LUN）
    *
    *      返回指向新的struct scsi_device实例的指针，
    *      如果出现异常（例如在给定地址没有设备响应），则返
    *      回ERR_PTR(-ENODEV)
    *
    *      是否阻塞：是
    *
    *      注意事项：本函数通常在添加HBA的SCSI总线扫描过程
    *      中由系统内部调用（即scsi_scan_host()执行期间）。因此，
    *      仅应在以下情况调用：HBA在scsi_scan_host()完成扫描后，
    *      又检测到新的SCSI设备（逻辑单元）。若成功执行，本次调用
    *      可能会触发LLD的以下回调函数：sdev_init()以及
    *      sdev_configure()
    *
    *      函数定义：drivers/scsi/scsi_scan.c
    **/
    struct scsi_device * scsi_add_device(struct Scsi_Host *shost,
                                        unsigned int channel,
                                        unsigned int id, unsigned int lun)


    /**
    * scsi_add_host - 执行sysfs注册并设置传输类
    * @shost:   指向SCSI主机适配器实例的指针
    * @dev:     指向scsi类设备结构体（struct device）的指针
    *
    *      成功返回0，失败返回负的errno（例如：-ENOMEM）
    *
    *      是否阻塞：否
    *
    *      注意事项：仅在“热插拔初始化模型”中需要调用，且必须在
    *      scsi_host_alloc()成功执行后调用。该函数不会扫描总线；
    *      总线扫描可通过调用scsi_scan_host()或其他传输层特定的
    *      方法完成。在调用该函数之前，LLD必须先设置好传输模板，
    *      并且只能在调用该函数之后才能访问传输类
    *      （transport class）相关的数据结构。
    *
    *      函数定义：drivers/scsi/hosts.c
    **/
    int scsi_add_host(struct Scsi_Host *shost, struct device * dev)


    /**
    * scsi_change_queue_depth - 调整SCSI设备队列深度
    * @sdev:       指向要更改队列深度的SCSI设备的指针
    * @tags        如果启用了标记队列，则表示允许的标记数，
    *              或者在非标记模式下，LLD可以排队的命令
    *              数（如 cmd_per_lun）。
    *
    *      无返回
    *
    *      是否阻塞：否
    *
    *      注意事项：可以在任何时刻调用该函数，只要该SCSI设备受该LLD控
    *      制。[具体来说，可以在sdev_configure()执行期间或之后，且在
    *      sdev_destroy()执行之前调用。] 该函数可安全地在中断上下文中
    *      调用。
    *
    *      函数定义：drivers/scsi/scsi.c [更多注释请参考源代码]
    **/
    int scsi_change_queue_depth(struct scsi_device *sdev, int tags)


    /**
    * scsi_bios_ptable - 返回块设备分区表的副本
    * @dev:        指向块设备的指针
    *
    *      返回指向分区表的指针，失败返回NULL
    *
    *      是否阻塞：是
    *
    *      注意事项：调用方负责释放返回的内存（通过 kfree() 释放）
    *
    *      函数定义：drivers/scsi/scsicam.c
    **/
    unsigned char *scsi_bios_ptable(struct block_device *dev)


    /**
    * scsi_block_requests - 阻止向指定主机提交新命令
    *
    * @shost: 指向特定主机的指针，用于阻止命令的发送
    *
    *      无返回
    *
    *      是否阻塞：否
    *
    *      注意事项：没有定时器或其他任何机制可以解除阻塞，唯一的方式
    *      是由LLD调用scsi_unblock_requests()方可恢复。
    *
    *      函数定义：drivers/scsi/scsi_lib.c
    **/
    void scsi_block_requests(struct Scsi_Host * shost)


    /**
    * scsi_host_alloc - 创建SCSI主机适配器实例并执行基础初始化
    * @sht:        指向SCSI主机模板的指针
    * @privsize:   在hostdata数组中分配的额外字节数（该数组是返
    *              回的Scsi_Host实例的最后一个成员）
    *
    *      返回指向新的Scsi_Host实例的指针，失败返回NULL
    *
    *      是否阻塞：是
    *
    *      注意事项：当此调用返回给LLD时，该主机适配器上的
    *      SCSI总线扫描尚未进行。hostdata数组（默认长度为
    *      零）是LLD专属的每主机私有区域，供LLD独占使用。
    *      两个相关的引用计数都被设置为1。完整的注册（位于
    *      sysfs）与总线扫描由scsi_add_host()和
    *      scsi_scan_host()稍后执行。
    *      函数定义：drivers/scsi/hosts.c
    **/
    struct Scsi_Host * scsi_host_alloc(const struct scsi_host_template * sht,
                                       int privsize)


    /**
    * scsi_host_get - 增加SCSI主机适配器实例的引用计数
    * @shost:   指向Scsi_Host实例的指针
    *
    *      无返回
    *
    *      是否阻塞：目前可能会阻塞，但可能迭代为不阻塞
    *
    *      注意事项：会同时增加struct Scsi_Host中两个子对
    *      象的引用计数
    *
    *      函数定义：drivers/scsi/hosts.c
    **/
    void scsi_host_get(struct Scsi_Host *shost)


    /**
    * scsi_host_put - 减少SCSI主机适配器实例的引用计数
    *                 （归零时释放）
    * @shost:   指向Scsi_Host实例的指针
    *
    *      无返回
    *
    *      是否阻塞：当前可能会阻塞，但可能会改为不阻塞
    *
    *      注意事项：实际会递减两个子对象中的计数。当后一个引用
    *      计数归零时系统会自动释放Scsi_Host实例。
    *      LLD 无需关注Scsi_Host实例的具体释放时机，只要在平衡
    *      引用计数使用后不再访问该实例即可。
    *      函数定义：drivers/scsi/hosts.c
    **/
    void scsi_host_put(struct Scsi_Host *shost)


    /**
    * scsi_remove_device - 卸载并移除SCSI设备
    * @sdev:      指向SCSI设备实例的指针
    *
    *      返回值：成功返回0，若设备未连接，则返回-EINVAL
    *
    *      是否阻塞：是
    *
    *      如果LLD发现某个SCSI设备（逻辑单元，lu）已经被移除，
    *      但其主机适配器实例依旧存在，则可以请求移除该SCSI设备。
    *      如果该调用成功将触发sdev_destroy()回调函数的执行。调
    *      用完成后，sdev将变成一个无效的指针。
    *
    *      函数定义：drivers/scsi/scsi_sysfs.c
    **/
    int scsi_remove_device(struct scsi_device *sdev)


    /**
    * scsi_remove_host - 卸载并移除主机控制器下的所有SCSI设备
    * @shost:      指向SCSI主机适配器实例的指针
    *
    *      返回值：成功返回0，失败返回1（例如：LLD正忙？？）
    *
    *      是否阻塞：是
    *
    *      注意事项：仅在使用“热插拔初始化模型”时调用。应在调用
    *      scsi_host_put()前调用。
    *
    *      函数定义：drivers/scsi/hosts.c
    **/
    int scsi_remove_host(struct Scsi_Host *shost)


    /**
    * scsi_report_bus_reset - 报告检测到的SCSI总线复位事件
    * @shost: 指向关联的SCSI主机适配器的指针
    * @channel: 发生SCSI总线复位的通道号
    *
    *      返回值：无
    *
    *      是否阻塞：否
    *
    *      注意事项：仅当复位来自未知来源时才需调用此函数。
    *      由SCSI中间层发起的复位无需调用，但调用也不会导
    *      致副作用。此函数的主要作用是确保系统能正确处理
    *      CHECK_CONDITION状态。
    *
    *      函数定义：drivers/scsi/scsi_error.c
    **/
    void scsi_report_bus_reset(struct Scsi_Host * shost, int channel)


    /**
    * scsi_scan_host - 执行SCSI总线扫描
    * @shost: 指向SCSI主机适配器实例的指针
    *
    *      是否阻塞：是
    *
    *      注意事项：应在调用scsi_add_host()后调用
    *
    *      函数定义：drivers/scsi/scsi_scan.c
    **/
    void scsi_scan_host(struct Scsi_Host *shost)


    /**
    * scsi_track_queue_full - 跟踪指定设备上连续的QUEUE_FULL
    *                         事件，以判断是否需要及何时调整
    *                         该设备的队列深度。
    * @sdev:  指向SCSI设备实例的指针
    * @depth: 当前该设备上未完成的SCSI命令数量（不包括返回
    *         QUEUE_FULL的命令）
    *
    *      返回值：0  - 当前队列深度无需调整
    *              >0 - 需要将队列深度调整为此返回值指定的新深度
    *              -1 - 需要回退到非标记操作模式，并使用
    *                   host->cmd_per_lun作为非标记命令队列的
    *                   深度限制
    *
    *      是否阻塞：否
    *
    *      注意事项：LLD可以在任意时刻调用该函数。系统将自动执行“正确
	*               的处理流程”；该函数支持在中断上下文中安全地调用
    *
    *      函数定义：drivers/scsi/scsi.c
    **/
    int scsi_track_queue_full(struct scsi_device *sdev, int depth)


    /**
    * scsi_unblock_requests - 恢复向指定主机适配器提交命令
    *
    * @shost: 指向要解除阻塞的主机适配器的指针
    *
    *      返回值：无
    *
    *      是否阻塞：否
    *
    *      函数定义：drivers/scsi/scsi_lib.c
    **/
    void scsi_unblock_requests(struct Scsi_Host * shost)



接口函数
========
接口函数由底层驱动（LLD）定义实现，其函数指针保存在
struct scsi_host_template实例中，并将该实例传递给
scsi_host_alloc()。
部分接口函数为必选实现项。所有
接口函数都应声明为static，约定俗成的命名规则如下，
驱动“xyz”应将其sdev_configure()函数声明为::

	static int xyz_sdev_configure(struct scsi_device * sdev);

其余接口函数的命名规范均依此类推。

需将该函数指针赋值给“struct scsi_host_template”实例
的‘sdev_configure’成员变量中，并将该结构体实例指针传
递到中间层的scsi_host_alloc()函数。

各个接口函数的详细说明可参考include/scsi/scsi_host.h
文件，具体描述位于“struct scsi_host_template”结构体
各个成员的上方。在某些情况下，scsi_host.h头文件中的描
述比本文提供的更为详尽。

以下按字母顺序列出所有接口函数及其说明。

摘要：

  - bios_param - 获取磁盘的磁头/扇区/柱面参数
  - eh_timed_out - SCSI命令超时回调
  - eh_abort_handler - 中止指定的SCSI命令
  - eh_bus_reset_handler - 触发SCSI总线复位
  - eh_device_reset_handler - 执行SCSI设备复位
  - eh_host_reset_handler - 复位主机（主机总线适配器）
  - info - 提供指定主机适配器的相关信息
  - ioctl - 驱动可响应ioctl控制命令
  - proc_info - 支持/proc/scsi/{驱动名}/{主机号}文件节点的读写操作
  - queuecommand - 将SCSI命令提交到主机控制器，命令执行完成后调用‘done’回调
  - sdev_init - 在向新设备发送SCSI命令前的初始化
  - sdev_configure - 设备挂载后的精细化微调
  - sdev_destroy - 设备即将被移除前的清理


详细信息::

    /**
    *      bios_param - 获取磁盘的磁头/扇区/柱面参数
    *      @sdev: 指向SCSI设备实例的指针（定义于
    *             include/scsi/scsi_device.h中）
    *      @bdev: 指向块设备实例的指针（定义于fs.h中）
    *      @capacity: 设备容量（以512字节扇区为单位）
    *      @params: 三元数组用于保存输出结果：
    *              params[0]：磁头数量（最大255）
    *              params[1]：扇区数量（最大63）
    *              params[2]：柱面数量
    *
    *      返回值：被忽略
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 进程上下文（sd）
    *
    *      注意事项: 若未提供此函数，系统将基于READ CAPACITY
    *      使用默认几何参数。params数组已预初始化伪值，防止函
    *      数无输出。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int bios_param(struct scsi_device * sdev, struct block_device *bdev,
		    sector_t capacity, int params[3])


    /**
    *      eh_timed_out - SCSI命令超时回调
    *      @scp: 标识超时的命令
    *
    *      返回值:
    *
    *      EH_HANDLED:             我已修复该错误，请继续完成该命令
    *      EH_RESET_TIMER:         我需要更多时间，请重置定时器并重新开始计时
    *      EH_NOT_HANDLED          开始正常的错误恢复流程
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 中断上下文
    *
    *      注意事项: 该回调函数为LLD提供一个机会进行本地
    *      错误恢复处理。此处的恢复仅限于判断该未完成的命
    *      令是否还有可能完成。此回调中不允许中止或重新启
    *      动该命令。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int eh_timed_out(struct scsi_cmnd * scp)


    /**
    *      eh_abort_handler - 中止指定的SCSI命令
    *      @scp: 标识要中止的命令
    *
    *      返回值：如果命令成功中止，则返回SUCCESS，否则返回FAILED
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 内核线程
    *
    *      注意事项: 该函数仅在命令超时时才被调用。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int eh_abort_handler(struct scsi_cmnd * scp)


    /**
    *      eh_bus_reset_handler -  发起SCSI总线复位
    *      @scp: 包含该设备的SCSI总线应进行重置
    *
    *      返回值：重置成功返回SUCCESS；否则返回FAILED
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 内核线程
    *
    *      注意事项: 由SCSI错误处理线程（scsi_eh）调用。
    *      在错误处理期间，当前主机适配器的所有IO请求均
    *      被阻塞。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int eh_bus_reset_handler(struct scsi_cmnd * scp)


    /**
    *      eh_device_reset_handler - 发起SCSI设备复位
    *      @scp: 指定将被重置的SCSI设备
    *
    *      返回值：如果命令成功中止返回SUCCESS，否则返回FAILED
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 内核线程
    *
    *      注意事项: 由SCSI错误处理线程（scsi_eh）调用。
    *      在错误处理期间，当前主机适配器的所有IO请求均
    *      被阻塞。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int eh_device_reset_handler(struct scsi_cmnd * scp)


    /**
    *      eh_host_reset_handler - 复位主机（主机总线适配器）
    *      @scp: 管理该设备的SCSI主机适配器应该被重置
    *
    *      返回值：如果命令成功中止返回SUCCESS，否则返回FAILED
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 内核线程
    *
    *      注意事项: 由SCSI错误处理线程（scsi_eh）调用。
    *      在错误处理期间，当前主机适配器的所有IO请求均
    *      被阻塞。当使用默认的eh_strategy策略时，如果
    *      _abort_、_device_reset_、_bus_reset_和该处
    *      理函数均未定义（或全部返回FAILED），系统强制
    *      该故障设备处于离线状态
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int eh_host_reset_handler(struct scsi_cmnd * scp)


    /**
    *      info - 提供给定主机适配器的详细信息：驱动程序名称
    *             以及用于区分不同主机适配器的数据结构
    *      @shp: 指向目标主机的struct Scsi_Host实例
    *
    *      返回值：返回以NULL结尾的ASCII字符串。[驱动
    *      负责管理返回的字符串所在内存并确保其在整个
    *      主机适配器生命周期内有效。]
    *
    *      并发安全声明: 无锁
    *
    *      调用上下文说明: 进程上下文
    *
    *      注意事项: 通常提供诸如I/O地址或中断号
    *      等PCI或ISA信息。如果未实现该函数，则
    *      默认使用struct Scsi_Host::name 字段。
    *      返回的字符串应为单行（即不包含换行符）。
    *      通过SCSI_IOCTL_PROBE_HOST ioctl可获
    *      取该函数返回的字符串，如果该函数不可用，
    *      则ioctl返回struct Scsi_Host::name中
    *      的字符串。

    *
    *      可选实现说明：由LLD选择性定义
    **/
    const char * info(struct Scsi_Host * shp)


    /**
    *      ioctl - 驱动可响应ioctl控制命令
    *      @sdp: ioctl操作针对的SCSI设备
    *      @cmd: ioctl命令号
    *      @arg: 指向用户空间读写数据的指针。由于他指向用
    *            户空间，必须使用适当的内核函数
    *            （如 copy_from_user()）。按照Unix的风
    *            格，该参数也可以视为unsigned long 类型。
    *
    *      返回值：如果出错则返回负的“errno”值。返回0或正值表
    *      示成功，并将返回值传递给用户空间。
    *
    *      并发安全声明：无锁
    *
    *      调用上下文说明：进程上下文
    *
    *      注意事项：SCSI子系统使用“逐层下传
    *      （trickle down）”的ioctl模型。
    *      用户层会对上层驱动设备节点
    *      （例如/dev/sdc）发起ioctl()调用，
    *      如果上层驱动无法识别该命令，则将其
    *      传递给SCSI中间层，若中间层也无法识
    *      别，则再传递给控制该设备的LLD。
    *      根据最新的Unix标准，对于不支持的
    *      ioctl()命令，应返回-ENOTTY。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int ioctl(struct scsi_device *sdp, int cmd, void *arg)


    /**
    *      proc_info - 支持/proc/scsi/{驱动名}/{主机号}文件节点的读写操作
    *      @buffer: 输入或出的缓冲区锚点（writeto1_read0==0表示向buffer写
    *               入，writeto1_read0==1表示由buffer读取）
    *      @start: 当writeto1_read0==0时，用于指定驱动实际填充的起始位置；
    *              当writeto1_read0==1时被忽略。
    *      @offset: 当writeto1_read0==0时，表示用户关注的数据在缓冲区中的
    *               偏移。当writeto1_read0==1时忽略。
    *      @length: 缓冲区的最大（或实际使用）长度
    *      @host_no: 目标SCSI Host的编号（struct Scsi_Host::host_no）
    *      @writeto1_read0: 1 -> 表示数据从用户空间写入驱动
    *                        （例如，“echo some_string > /proc/scsi/xyz/2”）
    *                       0 -> 表示用户从驱动读取数据
    *                        （例如，“cat /proc/scsi/xyz/2”）
    *
    *      返回值：当writeto1_read0==1时返回写入长度。否则，
    *      返回从offset偏移开始输出到buffer的字符数。
    *
    *      并发安全声明：无锁
    *
    *      调用上下文说明：进程上下文
    *
    *      注意事项：该函数由scsi_proc.c驱动，与proc_fs交互。
    *      当前SCSI子系统可移除对proc_fs的支持，相关配置选。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int proc_info(char * buffer, char ** start, off_t offset,
                  int length, int host_no, int writeto1_read0)


    /**
    *      queuecommand - 将SCSI命令提交到主机控制器，命令执行完成后调用scp->scsi_done回调函数
    *      @shost: 指向目标SCSI主机控制器
    *      @scp: 指向待处理的SCSI命令
    *
    *      返回值：成功返回0。
    *
    *      如果发生错误，则返回：
    *
    *      SCSI_MLQUEUE_DEVICE_BUSY表示设备队列满，
    *      SCSI_MLQUEUE_HOST_BUSY表示整个主机队列满
    *
    *      在这两种情况下，中间层将自动重新提交该I/O请求
    *
    *      - 若返回SCSI_MLQUEUE_DEVICE_BUSY，则仅暂停该
    *      特定设备的命令处理，当该设备的某个命令完成返回
    *      时（或在短暂延迟后如果没有其他未完成命令）将恢
    *      复其处理。其他设备的命令仍正常继续处理。
    *
    *      - 若返回SCSI_MLQUEUE_HOST_BUSY，将暂停该主机
    *      的所有I/O操作，当任意命令从该主机返回时（或在
    *      短暂延迟后如果没有其他未完成命令）将恢复处理。
    *
    *      为了与早期的queuecommand兼容，任何其他返回值
    *      都被视作SCSI_MLQUEUE_HOST_BUSY。
    *
    *      对于其他可立即检测到的错误，可通过以下流程处
    *      理：设置scp->result为适当错误值，调用scp->scsi_done
    *      回调函数，然后该函数返回0。若该命令未立即执行（LLD
    *      正在启动或将要启动该命令），则应将scp->result置0并
    *      返回0。
    *
    *      命令所有权说明：若驱动返回0，则表示驱动获得该命令的
    *      所有权，
    *      并必须确保最终执行scp->scsi_done回调函数。注意：驱动
    *      可以在返回0之前调用scp->scsi_done，但一旦调用该回
    *      调函数后，就只能返回0。若驱动返回非零值，则禁止在任何时
    *      刻执行该命令的scsi_done回调函数。
    *
    *      并发安全声明：在2.6.36及更早的内核版本中，调用该函数时持有
    *      struct Scsi_Host::host_lock锁（通过“irqsave”获取中断安全的自旋锁），
    *      并且返回时仍需保持该锁；从Linux 2.6.37开始，queuecommand
    *      将在无锁状态下被调用。
    *
    *      调用上下文说明：在中断（软中断）或进程上下文中
    *
    *      注意事项：该函数执行应当非常快速，通常不会等待I/O
    *      完成。因此scp->scsi_done回调函数通常会在该函数返
    *      回后的某个时刻被调用（经常直接从中断服务例程中调用）。
    *      某些情况下（如模拟SCSI INQUIRY响应的伪适配器驱动），
    *      scp->scsi_done回调可能在该函数返回前就被调用。
    *      若scp->scsi_done回调函数未在指定时限内被调用，SCSI中
    *      间层将启动错误处理流程。当调用scp->scsi_done回调函数
    *      时，若“result”字段被设置为CHECK CONDITION，
    *      则LLD应执行自动感知并填充
    *      struct scsi_cmnd::sense_buffer数组。在中间层将
    *      命令加入LLD队列之前前，scsi_cmnd::sense_buffer数组
    *      会被清零。
    *
    *      可选实现说明：LLD必须实现
    **/
    int queuecommand(struct Scsi_Host *shost, struct scsi_cmnd * scp)


    /**
    *      sdev_init - 在向新设备发送任何SCSI命令前（即开始扫描
    *                  之前）调用该函数
    *      @sdp: 指向即将被扫描的新设备的指针
    *
    *      返回值：返回0表示正常。返回其他值表示出错，
    *      该设备将被忽略。
    *
    *      并发安全声明：无锁
    *
    *      调用上下文说明：进程上下文
    *
    *      注意事项：该函数允许LLD在设备首次扫描前分配所需的资源。
    *      对应的SCSI设备可能尚未真正存在，但SCSI中间层即将对其进
    *      行扫描（例如发送INQUIRY命令等）。如果设备存在，将调用
    *      sdev_configure()进行配置；如果设备不存在，则调用
    *      sdev_destroy()销毁。更多细节请参考
    *      include/scsi/scsi_host.h文件。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int sdev_init(struct scsi_device *sdp)


    /**
    *      sdev_configure - 在设备首次完成扫描（即已成功响应INQUIRY
    *                       命令）之后，LDD可调用该函数对设备进行进一步配置
    *      @sdp: 已连接的设备
    *
    *      返回值：返回0表示成功。任何其他返回值都被视为错误，此时
    *      设备将被标记为离线。[被标记离线的设备不会调用sdev_destroy()，
    *      因此需要LLD主动清理资源。]
    *
    *      并发安全声明：无锁
    *
    *      调用上下文说明：进程上下文
    *
    *      注意事项：该接口允许LLD查看设备扫描代码所发出的初始INQUIRY
    *      命令的响应，并采取对应操作。具体实现细节请参阅
    *      include/scsi/scsi_host.h文件。
    *
    *      可选实现说明：由LLD选择性定义
    **/
    int sdev_configure(struct scsi_device *sdp)


    /**
    *      sdev_destroy - 当指定设备即将被关闭时调用。此时该设备
    *                     上的所有I/O活动均已停止。
    *      @sdp: 即将关闭的设备
    *
    *      返回值：无
    *
    *      并发安全声明：无锁
    *
    *      调用上下文说明：进程上下文
    *
    *      注意事项：该设备的中间层数据结构仍然存在
    *      但即将被销毁。驱动程序此时应当释放为该设
    *      备分配的所有专属资源。系统将不再向此sdp
    *      实例发送任何命令。[但该设备可能在未来被
    *      重新连接，届时将通过新的struct scsi_device
    *      实例，并触发后续的sdev_init()和
    *      sdev_configure()调用过程。]
    *
    *      可选实现说明：由LLD选择性定义
    **/
    void sdev_destroy(struct scsi_device *sdp)



数据结构
========
struct scsi_host_template
-------------------------
每个LLD对应一个“struct scsi_host_template”
实例 [#]_。该结构体通常被初始化为驱动头文件中的静
态全局变量，此方式可确保未显式初始化的成员自动置零
（0或NULL）。关键成员变量说明如下：

    name
		 - 驱动程序的名称（可以包含空格，请限制在80个字符以内）

    proc_name
		 - 在“/proc/scsi/<proc_name>/<host_no>”
		   和sysfs的“drivers”目录中使用的名称。因此
		   “proc_name”应仅包含Unix文件名中可接受
		   的字符。

    ``(*queuecommand)()``
		 - 中间层使用的主要回调函数，用于将SCSI命令
		   提交到LLD。

    vendor_id
		 - 该字段是一个唯一标识值，用于确认提供
		   Scsi_Host LLD的供应商，最常用于
		   验证供应商特定的消息请求。该值由标识符类型
		   和供应商特定值组成，有效格式描述请参阅
		   scsi_netlink.h头文件。

该结构体的完整定义及详细注释请参阅 ``include/scsi/scsi_host.h``。

.. [#] 在极端情况下，单个驱动需要控制多种不同类型的硬件时，驱动可
       能包含多个实例，（例如某个LLD驱动同时处理ISA和PCI两种类型
       的适配卡，并为每种硬件类型维护独立的
       struct scsi_host_template实例）。

struct Scsi_Host
----------------
每个由LLD控制的主机适配器对应一个struct Scsi_Host实例。
该结构体与struct scsi_host_template具有多个相同成员。
当创建struct Scsi_Host实例时（通过hosts.c中的
scsi_host_alloc()函数），这些通用成员会从LLD的
struct scsi_host_template实例初始化而来。关键成员说明
如下：

    host_no
		 - 系统范围内唯一的主机标识号，按升序从0开始分配
    can_queue
		 - 必须大于0，表示适配器可处理的最大并发命令数，禁
		   止向适配器发送超过此数值的命令数
    this_id
		 - 主机适配器的SCSI ID（SCSI启动器标识），若未知则
		   设置为-1
    sg_tablesize
		 - 主机适配器支持的最大散列表（scatter-gather）元素
		   数。设置为SG_ALL或更小的值可避免使用链式SG列表，
		   且最小值必须为1
    max_sectors
		 - 单个SCSI命令中允许的最大扇区数（通常为512字节/
		   扇区）。默认值为0，此时会使用
		   SCSI_DEFAULT_MAX_SECTORS（在scsi_host.h中定义），
		   当前该值为1024。因此，如果未定义max_sectors，则磁盘的
		   最大传输大小为512KB。注意：这个大小可能不足以支持
		   磁盘固件上传。
    cmd_per_lun
		 - 主机适配器的设备上，每个LUN可排队的最大命令数。
		   此值可通过LLD调用scsi_change_queue_depth()进行
		   调整。
    hostt
		 - 指向LLD struct scsi_host_template实例的指针，
		   当前struct Scsi_Host实例正是由此模板生成。
    hostt->proc_name
		 - LLD的名称，sysfs使用的驱动名。
    transportt
		 - 指向LLD struct scsi_transport_template实例的指
		   针（如果存在）。当前支持FC与SPI传输协议。
    hostdata[0]
		 - 为LLD在struct Scsi_Host结构体末尾预留的区域，大小由
		   scsi_host_alloc()的第二个参数(privsize)决定。

scsi_host结构体的完整定义详见include/scsi/scsi_host.h。

struct scsi_device
------------------
通常而言，每个SCSI逻辑单元（Logical Unit）对应一个该结构
的实例。连接到主机适配器的SCSI设备通过三个要素唯一标识：通
道号（Channel Number）、目标ID（Target ID）和逻辑单元号
（LUN）。
该结构体完整定义于include/scsi/scsi_device.h。

struct scsi_cmnd
----------------
该结构体实例用于在LLD与SCSI中间层之间传递SCSI命令
及其响应。SCSI中间层会确保：提交到LLD的命令数不超过
scsi_change_queue_depth()（或struct Scsi_Host::cmd_per_lun）
设定的上限，且每个SCSI设备至少分配一个struct scsi_cmnd实例。
关键成员说明如下：

    cmnd
		 - 包含SCSI命令的数组
    cmd_len
		 - SCSI命令的长度（字节为单位）
    sc_data_direction
		 - 数据的传输方向。请参考
		   include/linux/dma-mapping.h中的
		   “enum dma_data_direction”。
    result
		 - LLD在调用“done”之前设置该值。值为0表示命令成功
		   完成（并且所有数据（如果有）已成功在主机与SCSI
		   目标设备之间完成传输）。“result”是一个32位无符
		   号整数，可以视为2个相关字节。SCSI状态值位于最
		   低有效位。请参考include/scsi/scsi.h中的
		   status_byte()与host_byte()宏以及其相关常量。
    sense_buffer
		 - 这是一个数组（最大长度为SCSI_SENSE_BUFFERSIZE
		   字节），当SCSI状态（“result”的最低有效位）设为
		   CHECK_CONDITION（2）时，该数组由LLD填写。若
		   CHECK_CONDITION被置位，且sense_buffer[0]的高
		   半字节值为7，则中间层会认为sense_buffer数组
		   包含有效的SCSI感知数据；否则，中间层会发送
		   REQUEST_SENSE SCSI命令来获取感知数据。由于命令
		   排队的存在，后一种方式容易出错，因此建议LLD始终
		   支持“自动感知”。
    device
		 - 指向与该命令关联的scsi_device对象的指针。
    resid_len (通过调用scsi_set_resid() / scsi_get_resid()访问)
		 - LLD应将此无符号整数设置为请求的传输长度（即
		   “request_bufflen”）减去实际传输的字节数。“resid_len”
		   默认设置为0，因此如果LLD无法检测到数据欠载（不能报告溢出），
		   则可以忽略它。LLD应在调用“done”之前设置
		   “resid_len”。
    underflow
		 - 如果实际传输的字节数小于该字段值，LLD应将
		   DID_ERROR << 16赋值给“result”。并非所有
		   LLD都实现此项检查，部分LLD仅将错误信息输出
		   到日志，而未真正报告DID_ERROR。更推荐
		   的做法是由LLD实现“resid_len”的支持。

建议LLD在从SCSI目标设备进行数据传输时设置“resid_len”字段
（例如READ操作）。当这些数据传输的感知码是MEDIUM ERROR或
HARDWARE ERROR（有时也包括RECOVERED ERROR）时设置
resid_len尤为重要。在这种情况下，如果LLD无法确定接收了多
少数据，那么最安全的做法是表示没有接收到任何数据。例如：
为了表明没有接收到任何有效数据，LLD可以使用如下辅助函数::

    scsi_set_resid(SCpnt, scsi_bufflen(SCpnt));

其中SCpnt是一个指向scsi_cmnd对象的指针。如果表示仅接收到
三个512字节的数据块，可以这样设置resid_len::

    scsi_set_resid(SCpnt, scsi_bufflen(SCpnt) - (3 * 512));

scsi_cmnd结构体定义在 include/scsi/scsi_cmnd.h文件中。


锁
===
每个struct Scsi_Host实例都有一个名为default_lock
的自旋锁（spin_lock），它在scsi_host_alloc()函数
中初始化（该函数定义在hosts.c文件中）。在同一个函数
中，struct Scsi_Host::host_lock指针会被初始化为指
向default_lock。此后，SCSI中间层执行的加
锁和解锁操作都会使用host_lock指针。过去，驱动程序可
以重写host_lock指针，但现在不再允许这样做。


自动感知
========
自动感知（Autosense或auto-sense）在SAM-2规范中被定
义为：当SCSI命令完成状态为CHECK CONDITION时，“自动
将感知数据（sense data）返回给应用程序客户端”。底层
驱动（LLD）应当执行自动感知。当LLD检测到
CHECK CONDITION状态时，可通过以下任一方式完成：

	a) 要求SCSI协议（例如SCSI并行接口（SPI））在此
	   类响应中执行一次额外的数据传输

	b) 或由LLD主动发起REQUEST SENSE命令获取感知数据

无论采用哪种方式，当检测到CHECK CONDITION状态时，中
间层通过检查结构体scsi_cmnd::sense_buffer[0]的值来
判断LLD是否已执行自动感知。若该字节的高半字节为7
（或 0xf），则认为已执行自动感知；若该字节为其他值
（且此字节在每条命令执行前会被初始化为0），则中间层将
主动发起REQUEST SENSE命令。

在存在命令队列的场景下，保存失败命令感知数据的“nexus”
可能会在等待REQUEST SENSE命令期间变得不同步。因此，
最佳实践是由LLD执行自动感知。


自Linux内核2.4以来的变更
========================
io_request_lock已被多个细粒度锁替代。与底层驱动
（LLD）相关的锁是struct Scsi_Host::host_lock，且每
个SCSI主机都独立拥有一个该锁。

旧的错误处理机制已经被移除。这意味着LLD的接口函数
abort()与reset()已经被删除。
struct scsi_host_template::use_new_eh_code标志
也已经被移除。

在Linux内核2.4中，SCSI子系统的配置描述与其他Linux子系
统的配置描述集中存放在Documentation/Configure.help
文件中。在Linux内核2.6中，SCSI子系统拥有独立的配置文
件drivers/scsi/Kconfig（体积更小），同时包含配置信息
与帮助信息。

struct SHT已重命名为struct scsi_host_template。

新增“热插拔初始化模型”以及许多用于支持该功能的额外函数。


致谢
====
以下人员对本文档做出了贡献：

	- Mike Anderson <andmike at us dot ibm dot com>
	- James Bottomley <James dot Bottomley at hansenpartnership dot com>
	- Patrick Mansfield <patmans at us dot ibm dot com>
	- Christoph Hellwig <hch at infradead dot org>
	- Doug Ledford <dledford at redhat dot com>
	- Andries Brouwer <Andries dot Brouwer at cwi dot nl>
	- Randy Dunlap <rdunlap at xenotime dot net>
	- Alan Stern <stern at rowland dot harvard dot edu>


Douglas Gilbert
dgilbert at interlog dot com

2004年9月21日
