.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scheduler/sched-debug.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

=============
调度器debugfs
=============

用配置项CONFIG_SCHED_DEBUG=y启动内核后，将可以访问/sys/kernel/debug/sched
下的调度器专用调试文件。其中一些文件描述如下。

numa_balancing
==============

`numa_balancing` 目录用来存放控制非统一内存访问（NUMA）平衡特性的相关文件。
如果该特性导致系统负载太高，那么可以通过 `scan_period_min_ms, scan_delay_ms,
scan_period_max_ms, scan_size_mb` 文件控制NUMA缺页的内核采样速率。


scan_period_min_ms, scan_delay_ms, scan_period_max_ms, scan_size_mb
-------------------------------------------------------------------

自动NUMA平衡会扫描任务地址空间，检测页面是否被正确放置，或者数据是否应该被
迁移到任务正在运行的本地内存结点，此时需解映射页面。每个“扫描延迟”（scan delay）
时间之后，任务扫描其地址空间中下一批“扫描大小”（scan size）个页面。若抵达
内存地址空间末尾，扫描器将从头开始重新扫描。

结合来看，“扫描延迟”和“扫描大小”决定扫描速率。当“扫描延迟”减小时，扫描速率
增加。“扫描延迟”和每个任务的扫描速率都是自适应的，且依赖历史行为。如果页面被
正确放置，那么扫描延迟就会增加；否则扫描延迟就会减少。“扫描大小”不是自适应的，
“扫描大小”越大，扫描速率越高。

更高的扫描速率会产生更高的系统开销，因为必须捕获缺页异常，并且潜在地必须迁移
数据。然而，当扫描速率越高，若工作负载模式发生变化，任务的内存将越快地迁移到
本地结点，由于远程内存访问而产生的性能影响将降到最低。下面这些文件控制扫描延迟
的阈值和被扫描的页面数量。

``scan_period_min_ms`` 是扫描一个任务虚拟内存的最小时间，单位是毫秒。它有效地
控制了每个任务的最大扫描速率。

``scan_delay_ms`` 是一个任务初始化创建（fork）时，第一次使用的“扫描延迟”。

``scan_period_max_ms`` 是扫描一个任务虚拟内存的最大时间，单位是毫秒。它有效地
控制了每个任务的最小扫描速率。

``scan_size_mb`` 是一次特定的扫描中，要扫描多少兆字节（MB）对应的页面数。
