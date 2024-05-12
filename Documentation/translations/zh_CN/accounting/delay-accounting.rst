.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/accounting/delay-accounting.rst

:Translator: Yang Yang <yang.yang29@zte.com.cn>

========
延时计数
========

任务在等待某些内核资源可用时，会造成延时。例如一个可运行的任务可能会等待
一个空闲CPU来运行。

基于每任务的延时计数功能度量由以下情况造成的任务延时：

a) 等待一个CPU（任务为可运行）
b) 完成由该任务发起的块I/O同步请求
c) 页面交换
d) 内存回收
e) 抖动
f) 直接规整
g) 写保护复制

并将这些统计信息通过taskstats接口提供给用户空间。

这些延时信息为适当的调整任务CPU优先级、io优先级、rss限制提供反馈。重要任务
长期延时，表示可能需要提高其相关优先级。

通过使用taskstats接口，本功能还可提供一个线程组（对应传统Unix进程）所有任务
（或线程）的总延时统计信息。此类汇总往往是需要的，由内核来完成更加高效。

用户空间的实体，特别是资源管理程序，可将延时统计信息汇总到任意组中。为实现
这一点，任务的延时统计信息在其生命周期内和退出时皆可获取，从而确保可进行
连续、完整的监控。

接口
----

延时计数使用taskstats接口，该接口由本目录另一个单独的文档详细描述。Taskstats
向用户态返回一个通用数据结构，对应每pid或每tgid的统计信息。延时计数功能填写
该数据结构的特定字段。见

     include/uapi/linux/taskstats.h

其描述了延时计数相关字段。系统通常以计数器形式返回 CPU、同步块 I/O、交换、内存
回收、页缓存抖动、直接规整、写保护复制等的累积延时。

取任务某计数器两个连续读数的差值，将得到任务在该时间间隔内等待对应资源的总延时。

当任务退出时，内核会将包含每任务的统计信息发送给用户空间，而无需额外的命令。
若其为线程组最后一个退出的任务，内核还会发送每tgid的统计信息。更多详细信息见
taskstats接口的描述。

tools/accounting目录中的用户空间程序getdelays.c提供了一些简单的命令，用以显示
延时统计信息。其也是使用taskstats接口的示例。

用法
----

使用以下配置编译内核::

	CONFIG_TASK_DELAY_ACCT=y
	CONFIG_TASKSTATS=y

延时计数在启动时默认关闭。
若需开启，在启动参数中增加::

   delayacct

本文后续的说明基于延时计数已开启。也可在系统运行时，使用sysctl的
kernel.task_delayacct进行开关。注意，只有在启用延时计数后启动的
任务才会有相关信息。

系统启动后，使用类似getdelays.c的工具获取任务或线程组（tgid）的延时信息。

getdelays命令的一般格式::

	getdelays [-dilv] [-t tgid] [-p pid]

获取pid为10的任务从系统启动后的延时信息::

	# ./getdelays -d -p 10
	（输出信息和下例相似）

获取所有tgid为5的任务从系统启动后的总延时信息::

	# ./getdelays -d -t 5
	print delayacct stats ON
	TGID	5


	CPU             count     real total  virtual total    delay total  delay average
	                    8        7000000        6872122        3382277          0.423ms
	IO              count    delay total  delay average
	                    0              0              0.000ms
	SWAP            count    delay total  delay average
	                    0              0              0.000ms
	RECLAIM         count    delay total  delay average
	                    0              0              0.000ms
	THRASHING       count    delay total  delay average
	                    0              0              0.000ms
	COMPACT         count    delay total  delay average
	                    0              0              0.000ms
    WPCOPY          count    delay total  delay average
                       0              0              0ms

获取pid为1的IO计数，它只和-p一起使用::
	# ./getdelays -i -p 1
	printing IO accounting
	linuxrc: read=65536, write=0, cancelled_write=0

上面的命令与-v一起使用，可以获取更多调试信息。
