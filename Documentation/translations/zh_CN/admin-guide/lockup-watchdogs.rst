.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/lockup-watchdogs.rst
:Translator: Hailong Liu <liu.hailong6@zte.com.cn>

.. _cn_lockup-watchdogs:


=================================================
Softlockup与hardlockup检测机制(又名:nmi_watchdog)
=================================================

Linux中内核实现了一种用以检测系统发生softlockup和hardlockup的看门狗机制。

Softlockup是一种会引发系统在内核态中一直循环超过20秒（详见下面“实现”小节）导致
其他任务没有机会得到运行的BUG。一旦检测到'softlockup'发生，默认情况下系统会打
印当前堆栈跟踪信息并进入锁定状态。也可配置使其在检测到'softlockup'后进入panic
状态；通过sysctl命令设置“kernel.softlockup_panic”、使用内核启动参数
“softlockup_panic”（详见Documentation/admin-guide/kernel-parameters.rst）以及使
能内核编译选项“BOOTPARAM_SOFTLOCKUP_PANIC”都可实现这种配置。

而'hardlockup'是一种会引发系统在内核态一直循环超过10秒钟（详见"实现"小节）导致其
他中断没有机会运行的缺陷。与'softlockup'情况类似，除了使用sysctl命令设置
'hardlockup_panic'、使能内核选项“BOOTPARAM_HARDLOCKUP_PANIC”以及使用内核参数
"nmi_watchdog"(详见:”Documentation/admin-guide/kernel-parameters.rst“)外，一旦检
测到'hardlockup'默认情况下系统打印当前堆栈跟踪信息，然后进入锁定状态。

这个panic选项也可以与panic_timeout结合使用（这个panic_timeout是通过稍具迷惑性的
sysctl命令"kernel.panic"来设置），使系统在panic指定时间后自动重启。

实现
====

Softlockup和hardlockup分别建立在hrtimer(高精度定时器)和perf两个子系统上而实现。
这也就意味着理论上任何架构只要实现了这两个子系统就支持这两种检测机制。

Hrtimer用于周期性产生中断并唤醒watchdog线程；NMI perf事件则以”watchdog_thresh“
(编译时默认初始化为10秒，也可通过”watchdog_thresh“这个sysctl接口来进行配置修改)
为间隔周期产生以检测 hardlockups。如果一个CPU在这个时间段内没有检测到hrtimer中
断发生，'hardlockup 检测器'(即NMI perf事件处理函数)将会视系统配置而选择产生内核
警告或者直接panic。

而watchdog线程本质上是一个高优先级内核线程，每调度一次就对时间戳进行一次更新。
如果时间戳在2*watchdog_thresh(这个是softlockup的触发门限)这段时间都未更新,那么
"softlocup 检测器"(内部hrtimer定时器回调函数)会将相关的调试信息打印到系统日志中，
然后如果系统配置了进入panic流程则进入panic，否则内核继续执行。

Hrtimer定时器的周期是2*watchdog_thresh/5，也就是说在hardlockup被触发前hrtimer有
2~3次机会产生时钟中断。

如上所述,内核相当于为系统管理员提供了一个可调节hrtimer定时器和perf事件周期长度
的调节旋钮。如何通过这个旋钮为特定使用场景配置一个合理的周期值要对lockups检测的
响应速度和lockups检测开销这二者之间进行权衡。

默认情况下所有在线cpu上都会运行一个watchdog线程。不过在内核配置了”NO_HZ_FULL“的
情况下watchdog线程默认只会运行在管家(housekeeping)cpu上，而”nohz_full“启动参数指
定的cpu上则不会有watchdog线程运行。试想，如果我们允许watchdog线程在”nohz_full“指
定的cpu上运行，这些cpu上必须得运行时钟定时器来激发watchdog线程调度；这样一来就会
使”nohz_full“保护用户程序免受内核干扰的功能失效。当然，副作用就是”nohz_full“指定
的cpu即使在内核产生了lockup问题我们也无法检测到。不过，至少我们可以允许watchdog
线程在管家(non-tickless)核上继续运行以便我们能继续正常的监测这些cpus上的lockups
事件。

不论哪种情况都可以通过sysctl命令kernel.watchdog_cpumask来对没有运行watchdog线程
的cpu集合进行调节。对于nohz_full而言,如果nohz_full cpu上有异常挂住的情况，通过
这种方式打开这些cpu上的watchdog进行调试可能会有所作用。
