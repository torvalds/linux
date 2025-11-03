.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scsi/scsi_eh.rst

:翻译:

 郝栋栋 doubled <doubled@leap-io-kernel.com>

:校译:


===================
SCSI 中间层错误处理
===================

本文档描述了SCSI中间层（mid layer）的错误处理基础架构。
关于SCSI中间层的更多信息，请参阅：
Documentation/scsi/scsi_mid_low_api.rst。

.. 目录

	[1] SCSI 命令如何通过中间层传递并进入错误处理（EH）
		[1-1] scsi_cmnd（SCSI命令）结构体
		[1-2] scmd（SCSI 命令）是如何完成的？
			[1-2-1] 通过scsi_done完成scmd
			[1-2-2] 通过超时机制完成scmd
		[1-3] 错误处理模块如何接管流程
	[2] SCSI错误处理机制工作原理
		[2-1] 基于细粒度回调的错误处理
			[2-1-1] 概览
			[2-1-2] scmd在错误处理流程中的传递路径
			[2-1-3] 控制流分析
	[2-2] 通过transportt->eh_strategy_handler()实现的错误处理
		[2-2-1] transportt->eh_strategy_handler()调用前的中间层状态
		[2-2-2] transportt->eh_strategy_handler()调用后的中间层状态
		[2-2-3] 注意事项


1. SCSI命令在中间层及错误处理中的传递流程
=========================================

1.1 scsi_cmnd结构体
-------------------

每个SCSI命令都由struct scsi_cmnd（简称scmd）结构体
表示。scmd包含两个list_head类型的链表节点：scmd->list
与scmd->eh_entry。其中scmd->list是用于空闲链表或设备
专属的scmd分配链表，与错误处理讨论关联不大。而
scmd->eh_entry则是专用于命令完成和错误处理链表，除非
特别说明，本文讨论中所有scmd的链表操作均通过
scmd->eh_entry实现。


1.2 scmd是如何完成的？
----------------------

底层设备驱动（LLDD）在获取SCSI命令（scmd）后，存在两种
完成路径：底层驱动可通过调用hostt->queuecommand()时从
中间层传递的scsi_done回调函数主动完成命令，或者当命令未
及时完成时由块层（block layer）触发超时处理机制。


1.2.1 通过scsi_done回调完成SCSI命令
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

对于所有非错误处理（EH）命令，scsi_done()是其完成回调
函数。它只调用blk_mq_complete_request()来删除块层的
定时器并触发块设备软中断（BLOCK_SOFTIRQ）。

BLOCK_SOFTIRQ会间接调用scsi_complete()，进而调用
scsi_decide_disposition()来决定如何处理该命令。
scsi_decide_disposition()会查看scmd->result值和感
应码数据来决定如何处理命令。

 - SUCCESS

	调用scsi_finish_command()来处理该命令。该函数会
	执行一些维护操作，然后调用scsi_io_completion()来
	完成I/O操作。scsi_io_completion()会通过调用
	blk_end_request及其相关函数来通知块层该请求已完成，
	如果发生错误，还会判断如何处理剩余的数据。

 - NEEDS_RETRY

 - ADD_TO_MLQUEUE

	scmd被重新加入到块设备队列中。

 - otherwise

	调用scsi_eh_scmd_add(scmd)来处理该命令。
	关于此函数的详细信息，请参见 [1-3]。


1.2.2 scmd超时完成机制
^^^^^^^^^^^^^^^^^^^^^^

SCSI命令超时处理机制由scsi_timeout()函数实现。
当发生超时事件时，该函数

 1. 首先调用可选的hostt->eh_timed_out()回调函数。
    返回值可能是以下3种情况之一：

	- ``SCSI_EH_RESET_TIMER``
		表示需要延长命令执行时间并重启计时器。

	- ``SCSI_EH_NOT_HANDLED``
		表示eh_timed_out()未处理该命令。
		此时将执行第2步的处理流程。

	- ``SCSI_EH_DONE``
		表示eh_timed_out()已完成该命令。

 2. 若未通过回调函数解决，系统将调用
    scsi_abort_command()发起异步中止操作，该操作最多
    可执行scmd->allowed + 1次。但存在三种例外情况会跳
    过异步中止而直接进入第3步处理：当检测到
    SCSI_EH_ABORT_SCHEDULED标志位已置位（表明该命令先
    前已被中止过一次且当前重试仍失败）、当重试次数已达上
    限、或当错误处理时限已到期时。在这些情况下，系统将跳
    过异步中止流程而直接执行第3步处理方案。

 3. 最终未解决的命令会通过scsi_eh_scmd_add(scmd)移交给
    错误处理子系统，具体流程详见[1-4]章节说明。

1.3 异步命令中止机制
--------------------

当命令超时触发后，系统会通过scsi_abort_command()调度异
步中止操作。若中止操作执行成功，则根据重试次数决定后续处
理：若未达最大重试限制，命令将重新下发执行；若重试次数已
耗尽，则命令最终以DID_TIME_OUT状态终止。当中止操作失败
时，系统会调用scsi_eh_scmd_add()将该命令移交错误处理子
系统，具体处理流程详见[1-4]。

1.4 错误处理(EH)接管机制
------------------------

SCSI命令通过scsi_eh_scmd_add()函数进入错误处理流程，该函
数执行以下操作：

 1. 将scmd->eh_entry链接到shost->eh_cmd_q

 2. 在shost->shost_state中设置SHOST_RECOVERY状态位

 3. 递增shost->host_failed失败计数器

 4. 当检测到shost->host_busy == shost->host_failed
    时（即所有进行中命令均已失败）立即唤醒SCSI错误处理
    线程。

如上所述，当任一scmd被加入到shost->eh_cmd_q队列时，系统
会立即置位shost_state中的SHOST_RECOVERY状态标志位，该操
作将阻止块层向对应主机控制器下发任何新的SCSI命令。在此状
态下，主机控制器上所有正在处理的scmd最终会进入以下三种状
态之一：正常完成、失败后被移入到eh_cmd_q队列、或因超时被
添加到shost->eh_cmd_q队列。

如果所有的SCSI命令都已经完成或失败，系统中正在执行的命令
数量与失败命令数量相等（
即shost->host_busy == shost->host_failed），此时将唤
醒SCSI错误处理线程。SCSI错误处理线程一旦被唤醒，就可以确
保所有未完成命令均已标记为失败状态，并且已经被链接到
shost->eh_cmd_q队列中。

需要特别说明的是，这并不意味着底层处理流程完全静止。当底层
驱动以错误状态完成某个scmd时，底层驱动及其下层组件会立刻遗
忘该命令的所有关联状态。但对于超时命令，除非
hostt->eh_timed_out()回调函数已经明确通知底层驱动丢弃该
命令（当前所有底层驱动均未实现此功能），否则从底层驱动视角
看该命令仍处于活跃状态，理论上仍可能在某时刻完成。当然，由
于超时计时器早已触发，所有此类延迟完成都将被系统直接忽略。

我们将在后续章节详细讨论关于SCSI错误处理如何执行中止操作（
即强制底层驱动丢弃已超时SCSI命令）。


2. SCSI错误处理机制详解
=======================

SCSI底层驱动可以通过以下两种方式之一来实现SCSI错误处理。

 - 细粒度的错误处理回调机制
	底层驱动可选择实现细粒度的错误处理回调函数，由SCSI中间层
	主导错误恢复流程并自动调用对应的回调函数。此实现模式的详
	细设计规范在[2-1]节中展开讨论。

 - eh_strategy_handler()回调函数
	该回调函数作为统一的错误处理入口，需要完整实现所有的恢复
	操作。具体而言，它必须涵盖SCSI中间层在常规恢复过程中执行
	的全部处理流程，相关实现将在[2-2]节中详细描述。

当错误恢复流程完成后，SCSI错误处理系统通过调用
scsi_restart_operations()函数恢复正常运行，该函数按顺序执行
以下操作：

 1. 验证是否需要执行驱动器安全门锁定机制

 2. 清除shost_state中的SHOST_RECOVERY状态标志位

 3. 唤醒所有在shost->host_wait上等待的任务。如果有人调用了
    scsi_block_when_processing_errors()则会发生这种情况。
    （疑问：由于错误处理期间块层队列已被阻塞，为何仍需显式
    唤醒？）

 4. 强制激活该主机控制器下所有设备的I/O队列


2.1 基于细粒度回调的错误处理机制
--------------------------------

2.1.1 概述
^^^^^^^^^^^

如果不存在eh_strategy_handler()，SCSI中间层将负责驱动的
错误处理。错误处理（EH）的目标有两个：一是让底层驱动程序、
主机和设备不再维护已超时的SCSI命令（scmd）；二是使他们准备
好接收新命令。当一个SCSI命令（scmd）被底层遗忘且底层已准备
好再次处理或拒绝该命令时，即可认为该scmd已恢复。

为实现这些目标，错误处理（EH）会逐步执行严重性递增的恢复
操作。部分操作通过下发SCSI命令完成，而其他操作则通过调用
以下细粒度的错误处理回调函数实现。这些回调函数可以省略，
若被省略则默认始终视为执行失败。

::

	int (* eh_abort_handler)(struct scsi_cmnd *);
	int (* eh_device_reset_handler)(struct scsi_cmnd *);
	int (* eh_bus_reset_handler)(struct scsi_cmnd *);
	int (* eh_host_reset_handler)(struct scsi_cmnd *);

只有在低级别的错误恢复操作无法恢复部分失败的SCSI命令
（scmd）时，才会采取更高级别的恢复操作。如果最高级别的错误
处理失败，就意味着整个错误恢复（EH）过程失败，所有未能恢复
的设备被强制下线。

在恢复过程中，需遵循以下规则：

 - 错误恢复操作针对待处理列表eh_work_q中的失败的scmds执
   行。如果某个恢复操作成功恢复了一个scmd，那么该scmd会
   从eh_work_q链表中移除。

   需要注意的是，对某个scmd执行的单个恢复操作可能会恢复
   多个scmd。例如，对某个设备执行复位操作可能会恢复该设
   备上所有失败的scmd。

 - 仅当低级别的恢复操作完成且eh_work_q仍然非空时，才会
   触发更高级别的操作

 - SCSI错误恢复机制会重用失败的scmd来发送恢复命令。对于
   超时的scmd，SCSI错误处理机制会确保底层驱动在重用scmd
   前已不再维护该命令。

当一个SCSI命令（scmd）被成功恢复后，错误处理逻辑会通过
scsi_eh_finish_cmd()将其从待处理队列（eh_work_q）移
至错误处理的本地完成队列（eh_done_q）。当所有scmd均恢
复完成（即eh_work_q为空时），错误处理逻辑会调用
scsi_eh_flush_done_q()对这些已恢复的scmd进行处理，即
重新尝试或错误总终止（向上层通知失败）。

SCSI命令仅在满足以下全部条件时才会被重试：对应的SCSI设
备仍处于在线状态，未设置REQ_FAILFAST标志或递增后的
scmd->retries值仍小于scmd->allowed。

2.1.2 SCSI命令在错误处理过程中的流转路径
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

 1. 错误完成/超时

    :处理: 调用scsi_eh_scmd_add()处理scmd

	- 将scmd添加到shost->eh_cmd_q
	- 设置SHOST_RECOVERY标记位
	- shost->host_failed++

    :锁要求: shost->host_lock

 2. 启动错误处理（EH）

    :操作: 将所有scmd移动到EH本地eh_work_q队列，并
	    清空 shost->eh_cmd_q。

    :锁要求: shost->host_lock（非严格必需，仅为保持一致性）

 3. scmd恢复

    :操作: 调用scsi_eh_finish_cmd()完成scmd的EH

	- 将scmd从本地eh_work_q队列移至本地eh_done_q队列

    :锁要求: 无

    :并发控制: 每个独立的eh_work_q至多一个线程，确保无锁
	    队列的访问

 4. EH完成

    :操作: 调用scsi_eh_flush_done_q()重试scmd或通知上层处理
	    失败。此函数可以被并发调用，但每个独立的eh_work_q队
	    列至多一个线程，以确保无锁队列的访问。

		- 从eh_done_q队列中移除scmd，清除scmd->eh_entry
		- 如果需要重试，调用scsi_queue_insert()重新入队scmd
		- 否则，调用scsi_finish_command()完成scmd
		- 将shost->host_failed置为零

    :锁要求: 队列或完成函数会执行适当的加锁操作


2.1.3 控制流
^^^^^^^^^^^^

 通过细粒度回调机制执行的SCSI错误处理（EH）是从
 scsi_unjam_host()函数开始的

``scsi_unjam_host``

    1. 持有shost->host_lock锁，将shost->eh_cmd_q中的命令移动
       到本地的eh_work_q队里中，并释放host_lock锁。注意，这一步
       会清空shost->eh_cmd_q。

    2. 调用scsi_eh_get_sense函数。

    ``scsi_eh_get_sense``

      该操作针对没有有效感知数据的错误完成命令。大部分SCSI传输协议
      或底层驱动在命令失败时会自动获取感知数据（自动感知）。出于性
      能原因，建议使用自动感知，推荐使用自动感知机制，因为它不仅有
      助于提升性能，还能避免从发生CHECK CONDITION到执行本操作之间，
      感知信息出现不同步的问题。

      注意，如果不支持自动感知，那么在使用scsi_done()以错误状态完成
      scmd 时，scmd->sense_buffer将包含无效感知数据。在这种情况下，
      scsi_decide_disposition()总是返回FAILED从而触发SCSI错误处理
      （EH）。当该scmd执行到这里时，会重新获取感知数据，并再次调用
      scsi_decide_disposition()进行处理。

      1. 调用scsi_request_sense()发送REQUEST_SENSE命令。如果失败，
         则不采取任何操作。请注意，不采取任何操作会导致对该scmd执行
         更高级别的恢复操作。

      2. 调用scsi_decide_disposition()处理scmd

            - SUCCESS
				scmd->retries被设置为scmd->allowed以防止
				scsi_eh_flush_done_q()重试该scmd，并调用
				scsi_eh_finish_cmd()。

            - NEEDS_RETRY
				调用scsi_eh_finish_cmd()

            - 其他情况
				无操作。

    4. 如果!list_empty(&eh_work_q)，则调用scsi_eh_ready_devs()。

    ``scsi_eh_ready_devs``

	该函数采取四种逐步增强的措施，使失败的设备准备好处理新的命令。

	1. 调用scsi_eh_stu()

	``scsi_eh_stu``

	    对于每个具有有效感知数据且scsi_check_sense()判断为失败的
	    scmd发送START STOP UNIT（STU）命令且将start置1。注意，由
	    于我们明确选择错误完成的scmd，可以确定底层驱动已不再维护该
	    scmd，我们可以重用它进行STU。

	    如果STU操作成功且sdev处于离线或就绪状态，所有在sdev上失败的
	    scmd都会通过scsi_eh_finish_cmd()完成。

	    *注意* 如果hostt->eh_abort_handler()未实现或返回失败，可能
	    此时仍有超时的scmd，此时STU不会导致底层驱动不再维护scmd。但
	    是，如果STU执行成功，该函数会通过scsi_eh_finish_cmd()来完成
	    sdev上的所有scmd，这会导致底层驱动处于不一致的状态。看来STU
	    操作应仅在sdev不包含超时scmd时进行。

	2. 如果!list_empty(&eh_work_q)，调用scsi_eh_bus_device_reset()。

	``scsi_eh_bus_device_reset``

	    此操作与scsi_eh_stu()非常相似，区别在于使用
	    hostt->eh_device_reset_handler()替代STU命令。此外，由于我们
	    没有发送SCSI命令且重置会清空该sdev上所有的scmd，所以无需筛选错
	    误完成的scmd。

	3. 如果!list_empty(&eh_work_q)，调用scsi_eh_bus_reset()。

	``scsi_eh_bus_reset``

	    对于每个包含失败scmd的SCSI通道调用
	    hostt->eh_bus_reset_handler()。如果总线重置成功，那么该通道上
	    所有准备就绪或离线状态sdev上的失败scmd都会被处理处理完成。

	4. 如果!list_empty(&eh_work_q)，调用scsi_eh_host_reset()。

	``scsi_eh_host_reset``

	    调用hostt->eh_host_reset_handler()是最终的手段。如果SCSI主机
	    重置成功，主机上所有就绪或离线sdev上的失败scmd都会通过错误处理
	    完成。

	5. 如果!list_empty(&eh_work_q)，调用scsi_eh_offline_sdevs()。

	``scsi_eh_offline_sdevs``

	    离线所有包含未恢复scmd的所有sdev，并通过
	    scsi_eh_finish_cmd()完成这些scmd。

    5. 调用scsi_eh_flush_done_q()。

	``scsi_eh_flush_done_q``

	    此时所有的scmd都已经恢复（或放弃），并通过
	    scsi_eh_finish_cmd()函数加入eh_done_q队列。该函数通过
	    重试或显示通知上层scmd的失败来刷新eh_done_q。


2.2 基于transportt->eh_strategy_handler()的错误处理机制
-------------------------------------------------------------

在该机制中，transportt->eh_strategy_handler()替代
scsi_unjam_host()的被调用，并负责整个错误恢复过程。该处理
函数完成后应该确保底层驱动不再维护任何失败的scmd并且将设备
设置为就绪（准备接收新命令）或离线状态。此外，该函数还应该
执行SCSI错误处理的维护任务，以维护SCSI中间层的数据完整性。
换句话说，eh_strategy_handler()必须实现[2-1-2]中除第1步
外的所有步骤。


2.2.1 transportt->eh_strategy_handler()调用前的SCSI中间层状态
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

 进入该处理函数时，以下条件成立。

 - 每个失败的scmd的eh_flags字段已正确设置。

 - 每个失败的scmd通过scmd->eh_entry链接到scmd->eh_cmd_q队列。

 - 已设置SHOST_RECOVERY标志。

 - `shost->host_failed == shost->host_busy`。

2.2.2 transportt->eh_strategy_handler()调用后的SCSI中间层状态
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

 从该处理函数退出时，以下条件成立。

 - shost->host_failed为零。

 - shost->eh_cmd_q被清空。

 - 每个scmd->eh_entry被清空。

 - 对每个scmd必须调用scsi_queue_insert()或scsi_finish_command()。
   注意，该处理程序可以使用scmd->retries（剩余重试次数）和
   scmd->allowed（允许重试次数）限制重试次数。


2.2.3 注意事项
^^^^^^^^^^^^^^

 - 需明确已超时的scmd在底层仍处于活跃状态，因此在操作这些
   scmd前，必须确保底层已彻底不再维护。

 - 访问或修改shost数据结构时，必须持有shost->host_lock锁
   以维持数据一致性。

 - 错误处理完成后，每个故障设备必须彻底清除所有活跃SCSI命
   令（scmd）的关联状态。

 - 错误处理完成后，每个故障设备必须被设置为就绪（准备接收
   新命令）或离线状态。


Tejun Heo
htejun@gmail.com

11th September 2005
