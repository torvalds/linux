.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scsi/libsas.rst

:翻译:

 张钰杰 Yujie Zhang <yjzhang@leap-io-kernel.com>

:校译:

======
SAS 层
======

SAS 层是一个管理基础架构，用于管理 SAS LLDD。它位于 SCSI Core
与 SAS LLDD 之间。 体系结构如下: SCSI Core 关注的是 SAM/SPC 相
关的问题；SAS LLDD 及其序列控制器负责 PHY 层、OOB 信号以及链路
管理；而 SAS 层则负责以下任务::

      * SAS Phy、Port 和主机适配器（HA）事件管理（事件由 LLDD
        生成，由 SAS 层处理）；
      * SAS 端口的管理（创建与销毁）；
      * SAS 域的发现与重新验证；
      * SAS 域内设备的管理；
      * SCSI 主机的注册与注销；
      * 将设备注册到 SCSI Core（SAS 设备）或 libata（SATA 设备）；
      * 扩展器的管理，并向用户空间导出扩展器控制接口。

SAS LLDD 是一种 PCI 设备驱动程序。它负责 PHY 层和 OOB（带外）
信号的管理、厂商特定的任务，并向 SAS 层上报事件。

SAS 层实现了 SAS 1.1 规范中定义的大部分 SAS 功能。

sas_ha_struct 结构体用于向 SAS 层描述一个 SAS LLDD。该结构的
大部分字段由 SAS 层使用，但其中少数字段需要由 LLDD 进行初始化。

在完成硬件初始化之后，应当在驱动的 probe() 函数中调用
sas_register_ha()。该函数会将 LLDD 注册到 SCSI 子系统中，创
建一个对应的 SCSI 主机，并将你的 SAS 驱动程序注册到其在 sysfs
下创建的 SAS 设备树中。随后该函数将返回。接着，你需要使能 PHY，
以启动实际的 OOB（带外）过程；此时驱动将开始调用 notify_* 系
列事件回调函数。

结构体说明
==========

``struct sas_phy``
------------------

通常情况下，该结构体会被静态地嵌入到驱动自身定义的 PHY 结构体中,
例如::

    struct my_phy {
	    blah;
	    struct sas_phy sas_phy;
	    bleh;
    }

随后，在主机适配器（HA）的结构体中，所有的 PHY 通常以 my_phy
数组的形式存在（如下文所示）。

在初始化各个 PHY 时，除了初始化驱动自定义的 PHY 结构体外，还
需要同时初始化其中的 sas_phy 结构体。

一般来说，PHY 的管理由 LLDD 负责，而端口（port）的管理由 SAS
层负责。因此，PHY 的初始化与更新由 LLDD 完成，而端口的初始化与
更新则由 SAS 层完成。系统设计中规定，某些字段可由 LLDD 进行读
写，而 SAS 层只能读取这些字段；反之亦然。其设计目的是为了避免不
必要的锁操作。

在该设计中，某些字段可由 LLDD 进行读写（RW），而 SAS 层仅可读
取这些字段；反之亦然。这样设计的目的在于避免不必要的锁操作。

enabled
    - 必须设置(0/1)

id
    - 必须设置[0,MAX_PHYS)]

class, proto, type, role, oob_mode, linkrate
    - 必须设置。

oob_mode
    - 当 OOB（带外信号）完成后，设置此字段，然后通知 SAS 层。

sas_addr
    - 通常指向一个保存该 PHY 的 SAS 地址的数组，该数组可能位于
      驱动自定义的 my_phy 结构体中。

attached_sas_addr
    - 当 LLDD 接收到 IDENTIFY 帧或 FIS 帧时，应在通知 SAS 层
      之前设置该字段。其设计意图在于：有时 LLDD 可能需要伪造或
      提供一个与实际不同的 SAS 地址用于该 PHY/端口，而该机制允许
      LLDD 这样做。理想情况下，应将 SAS 地址从 IDENTIFY 帧中
      复制过来；对于直接连接的 SATA 设备，也可以由 LLDD 生成一
      个 SAS 地址。后续的发现过程可能会修改此字段。

frame_rcvd
    - 当接收到 IDENTIFY 或 FIS 帧时，将该帧复制到此处。正确的
      操作流程是获取锁 → 复制数据 → 设置 frame_rcvd_size → 释
      放锁 → 调用事件通知。该字段是一个指针，因为驱动无法精确确
      定硬件帧的大小；因此，实际的帧数据数组应定义在驱动自定义的
      PHY 结构体中，然后让此指针指向该数组。在持锁状态下，将帧从
      DMA 可访问内存区域复制到该数组中。

sas_prim
    - 用于存放接收到的原语（primitive）。参见 sas.h。操作流程同
      样是：获取锁 → 设置 primitive → 释放锁 → 通知事件。

port
    - 如果该 PHY 属于某个端口（port），此字段指向对应的 sas_port
      结构体。LLDD 仅可读取此字段。它由 SAS 层设置，用于指向当前
      PHY 所属的 sas_port。

ha
    - 可以由 LLDD 设置；但无论是否设置，SAS 层都会再次对其进行赋值。

lldd_phy
    - LLDD 应将此字段设置为指向自身定义的 PHY 结构体，这样当 SAS
      层调用某个回调并传入 sas_phy 时，驱动可以快速定位自身的 PHY
      结构体。如果 sas_phy 是嵌入式成员，也可以使用 container_of()
      宏进行访问——两种方式均可。

``struct sas_port``
-------------------

LLDD 不应修改该结构体中的任何字段——它只能读取这些字段。这些字段的
含义应当是不言自明的。

phy_mask 为 32 位，目前这一长度已足够使用，因为尚未听说有主机适配
器拥有超过8 个 PHY。

lldd_port
    - 目前尚无明确用途。不过，对于那些希望在 LLDD 内部维护自身端
      口表示的驱动，实现时可以利用该字段。

``struct sas_ha_struct``
------------------------

它通常静态声明在你自己的 LLDD 结构中，用于描述您的适配器::

    struct my_sas_ha {
	blah;
	struct sas_ha_struct sas_ha;
	struct my_phy phys[MAX_PHYS];
	struct sas_port sas_ports[MAX_PHYS]; /* (1) */
	bleh;
    };

    (1) 如果你的 LLDD 没有自己的端口表示

需要初始化（示例函数如下所示）。

pcidev
^^^^^^

sas_addr
       - 由于 SAS 层不想弄乱内存分配等, 因此这指向静态分配的数
         组中的某个位置（例如，在您的主机适配器结构中），并保存您或
         制造商等给出的主机适配器的 SAS 地址。

sas_port
^^^^^^^^

sas_phy
      - 指向结构体的指针数组（参见上文关于 sas_addr 的说明）。
        这些指针必须设置。更多细节见下文说明。

num_phys
       - 表示 sas_phy 数组中 PHY 的数量，同时也表示 sas_port
         数组中的端口数量。一个端口最多对应一个 PHY，因此最大端口数
         等于 num_phys。因此，结构中不再单独使用 num_ports 字段，
         而仅使用 num_phys。

事件接口::

	/* LLDD 调用以下函数来通知 SAS 类层发生事件 */
	void sas_notify_port_event(struct sas_phy *, enum port_event, gfp_t);
	void sas_notify_phy_event(struct sas_phy *, enum phy_event, gfp_t);

端口事件通知::

	/* SAS 类层调用以下回调来通知 LLDD 端口事件 */
	void (*lldd_port_formed)(struct sas_phy *);
	void (*lldd_port_deformed)(struct sas_phy *);

如果 LLDD 希望在端口形成或解散时接收通知，则应将上述回调指针设
置为符合函数类型定义的处理函数。

SAS LLDD 还应至少实现 SCSI 协议中定义的一种任务管理函数（TMFs）::

	/* 任务管理函数. 必须在进程上下文中调用 */
	int (*lldd_abort_task)(struct sas_task *);
	int (*lldd_abort_task_set)(struct domain_device *, u8 *lun);
	int (*lldd_clear_task_set)(struct domain_device *, u8 *lun);
	int (*lldd_I_T_nexus_reset)(struct domain_device *);
	int (*lldd_lu_reset)(struct domain_device *, u8 *lun);
	int (*lldd_query_task)(struct sas_task *);

如需更多信息，请参考 T10.org。

端口与适配器管理::

	/* 端口与适配器管理 */
	int (*lldd_clear_nexus_port)(struct sas_port *);
	int (*lldd_clear_nexus_ha)(struct sas_ha_struct *);

SAS LLDD 至少应实现上述函数中的一个。

PHY 管理::

	/* PHY 管理 */
	int (*lldd_control_phy)(struct sas_phy *, enum phy_func);

lldd_ha
    - 应设置为指向驱动的主机适配器（HA）结构体的指针。如果 sas_ha_struct
      被嵌入到更大的结构体中，也可以通过 container_of() 宏来获取。

一个示例的初始化与注册函数可以如下所示：（该函数应在 probe()
函数的最后调用）但必须在使能 PHY 执行 OOB 之前调用::

    static int register_sas_ha(struct my_sas_ha *my_ha)
    {
	    int i;
	    static struct sas_phy   *sas_phys[MAX_PHYS];
	    static struct sas_port  *sas_ports[MAX_PHYS];

	    my_ha->sas_ha.sas_addr = &my_ha->sas_addr[0];

	    for (i = 0; i < MAX_PHYS; i++) {
		    sas_phys[i] = &my_ha->phys[i].sas_phy;
		    sas_ports[i] = &my_ha->sas_ports[i];
	    }

	    my_ha->sas_ha.sas_phy  = sas_phys;
	    my_ha->sas_ha.sas_port = sas_ports;
	    my_ha->sas_ha.num_phys = MAX_PHYS;

	    my_ha->sas_ha.lldd_port_formed = my_port_formed;

	    my_ha->sas_ha.lldd_dev_found = my_dev_found;
	    my_ha->sas_ha.lldd_dev_gone = my_dev_gone;

	    my_ha->sas_ha.lldd_execute_task = my_execute_task;

	    my_ha->sas_ha.lldd_abort_task     = my_abort_task;
	    my_ha->sas_ha.lldd_abort_task_set = my_abort_task_set;
	    my_ha->sas_ha.lldd_clear_task_set = my_clear_task_set;
	    my_ha->sas_ha.lldd_I_T_nexus_reset= NULL; (2)
	    my_ha->sas_ha.lldd_lu_reset       = my_lu_reset;
	    my_ha->sas_ha.lldd_query_task     = my_query_task;

	    my_ha->sas_ha.lldd_clear_nexus_port = my_clear_nexus_port;
	    my_ha->sas_ha.lldd_clear_nexus_ha = my_clear_nexus_ha;

	    my_ha->sas_ha.lldd_control_phy = my_control_phy;

	    return sas_register_ha(&my_ha->sas_ha);
    }

(2) SAS 1.1 未定义 I_T Nexus Reset TMF（任务管理功能）。

事件
====

事件是 SAS LLDD 唯一的通知 SAS 层发生任何情况的方式。
LLDD 没有其他方法可以告知 SAS 层其内部或 SAS 域中发生的事件。

Phy 事件::

	PHYE_LOSS_OF_SIGNAL, (C)
	PHYE_OOB_DONE,
	PHYE_OOB_ERROR,      (C)
	PHYE_SPINUP_HOLD.

端口事件，通过 _phy_ 传递::

	PORTE_BYTES_DMAED,      (M)
	PORTE_BROADCAST_RCVD,   (E)
	PORTE_LINK_RESET_ERR,   (C)
	PORTE_TIMER_EVENT,      (C)
	PORTE_HARD_RESET.

主机适配器事件：
	HAE_RESET

SAS LLDD 应能够生成以下事件::

	- 来自 C 组的至少一个事件（可选），
	- 标记为 M（必需）的事件为必需事件（至少一种）；
	- 若希望 SAS 层处理域重新验证（domain revalidation），则
      应生成标记为 E（扩展器）的事件（仅需一种）；
	- 未标记的事件为可选事件。

含义

HAE_RESET
    - 当 HA 发生内部错误并被复位时。

PORTE_BYTES_DMAED
    - 在接收到 IDENTIFY/FIS 帧时。

PORTE_BROADCAST_RCVD
    - 在接收到一个原语时。

PORTE_LINK_RESET_ERR
    - 定时器超时、信号丢失、丢失 DWS 等情况。 [1]_

PORTE_TIMER_EVENT
    - DWS 复位超时定时器到期时。[1]_

PORTE_HARD_RESET
    - 收到 Hard Reset 原语。

PHYE_LOSS_OF_SIGNAL
    - 设备已断开连接。 [1]_

PHYE_OOB_DONE
    - OOB 过程成功完成，oob_mode 有效。

PHYE_OOB_ERROR
    - 执行 OOB 过程中出现错误，设备可能已断开。 [1]_

PHYE_SPINUP_HOLD
    - 检测到 SATA 设备，但未发送 COMWAKE 信号。

.. [1] 应设置或清除 phy 中相应的字段，或者从 tasklet 中调用
       内联函数 sas_phy_disconnected()，该函数只是一个辅助函数。

执行命令 SCSI RPC::

	int (*lldd_execute_task)(struct sas_task *, gfp_t gfp_flags);

用于将任务排队提交给 SAS LLDD，@task 为要执行的任务，@gfp_mask
为定义调用者上下文的 gfp 掩码。

此函数应实现 执行 SCSI RPC 命令。

也就是说，当调用 lldd_execute_task() 时，命令应当立即在传输
层发出。SAS LLDD 中在任何层级上都不应再进行队列排放。

返回值::

   * 返回 -SAS_QUEUE_FULL 或 -ENOMEM 表示未排入队列；
   * 返回 0 表示任务已成功排入队列。

::

    struct sas_task {
	    dev —— 此任务目标设备；
	    task_proto —— 协议类型，为 enum sas_proto 中的一种；
	    scatter —— 指向散布/聚集（SG）列表数组的指针；
	    num_scatter —— SG 列表元素数量；
	    total_xfer_len —— 预计传输的总字节数；
	    data_dir —— 数据传输方向(PCI_DMA_*)；
	    task_done —— 任务执行完成时的回调函数。
    };

发现
====

sysfs 树有以下用途::

    a) 它显示当前时刻 SAS 域的物理布局，即展示当前物理世界中
       域的实际结构。
    b) 显示某些设备的参数。 _at_discovery_time_.

下面是一个指向 tree(1) 程序的链接，该工具在查看 SAS 域时非常
有用：
ftp://mama.indstate.edu/linux/tree/

我期望用户空间的应用程序最终能够为此创建一个图形界面。

也就是说，sysfs 域树不会显示或保存某些状态变化，例如，如果你更
改了 READY LED 含义的设置，sysfs 树不会反映这种状态变化；但它
确实会显示域设备的当前连接状态。

维护内部设备状态变化的职责由上层（命令集驱动）和用户空间负责。

当某个设备或多个设备从域中拔出时，这一变化会立即反映在 sysfs
树中，并且这些设备会从系统中移除。

结构体 domain_device 描述了 SAS 域中的任意设备。它完全由 SAS
层管理。一个任务会指向某个域设备，SAS LLDD 就是通过这种方式知
道任务应发送到何处。SAS LLDD 只读取 domain_device 结构的内容，
但不会创建或销毁它。

用户空间中的扩展器管理
======================

在 sysfs 中的每个扩展器目录下，都有一个名为 "smp_portal" 的
文件。这是一个二进制的 sysfs 属性文件，它实现了一个 SMP 入口
（注意：这并不是一个 SMP 端口），用户空间程序可以通过它发送
SMP 请求并接收 SMP 响应。

该功能的实现方式看起来非常简单:

1. 构建要发送的 SMP 帧。其格式和布局在 SAS 规范中有说明。保持
   CRC 字段为 0。

open(2)

2. 以读写模式打开该扩展器的 SMP portal sysfs 文件。

write(2)

3. 将第 1 步中构建的帧写入文件。

read(2)

4. 读取与所构建帧预期返回长度相同的数据量。如果读取的数据量与
   预期不符，则表示发生了某种错误。

close(2)

整个过程在 "expander_conf.c" 文件中的函数 do_smp_func()
及其调用者中有详细展示。

对应的内核实现位于 "sas_expander.c" 文件中。

程序 "expander_conf.c" 实现了上述逻辑。它接收一个参数——扩展器
SMP portal 的 sysfs 文件名，并输出扩展器的信息，包括路由表内容。

SMP portal 赋予了你对扩展器的完全控制权，因此请谨慎操作。
