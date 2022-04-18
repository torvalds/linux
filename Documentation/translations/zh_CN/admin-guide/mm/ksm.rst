.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/mm/ksm.rst

:翻译:

  徐鑫 xu xin <xu.xin16@zte.com.cn>


============
内核同页合并
============


概述
====

KSM是一种能节省内存的数据去重功能，由CONFIG_KSM=y启用，并在2.6.32版本时被添
加到Linux内核。详见 ``mm/ksm.c`` 的实现，以及http://lwn.net/Articles/306704
和https://lwn.net/Articles/330589

KSM最初目的是为了与KVM（即著名的内核共享内存）一起使用而开发的，通过共享虚拟机
之间的公共数据，将更多虚拟机放入物理内存。但它对于任何会生成多个相同数据实例的
应用程序都是很有用的。

KSM的守护进程ksmd会定期扫描那些已注册的用户内存区域，查找内容相同的页面，这些
页面可以被单个写保护页面替换（如果进程以后想要更新其内容，将自动复制）。使用：
引用:`sysfs intraface  <ksm_sysfs>` 接口来配置KSM守护程序在单个过程中所扫描的页
数以及两个过程之间的间隔时间。

KSM只合并匿名（私有）页面，从不合并页缓存（文件）页面。KSM的合并页面最初只能被
锁定在内核内存中，但现在可以就像其他用户页面一样被换出（但当它们被交换回来时共
享会被破坏: ksmd必须重新发现它们的身份并再次合并）。

以madvise控制KSM
================

KSM仅在特定的地址空间区域时运行，即应用程序通过使用如下所示的madvise(2)系统调
用来请求某块地址成为可能的合并候选者的地址空间::

    int madvise(addr, length, MADV_MERGEABLE)

应用程序当然也可以通过调用::

    int madvise(addr, length, MADV_UNMERGEABLE)

来取消该请求，并恢复为非共享页面：此时KSM将去除合并在该范围内的任何合并页。注意：
这个去除合并的调用可能突然需要的内存量超过实际可用的内存量-那么可能会出现EAGAIN
失败，但更可能会唤醒OOM killer。

如果KSM未被配置到正在运行的内核中，则madvise MADV_MERGEABLE 和 MADV_UNMERGEABLE
的调用只会以EINVAL 失败。如果正在运行的内核是用CONFIG_KSM=y方式构建的，那么这些
调用通常会成功：即使KSM守护程序当前没有运行，MADV_MERGEABLE 仍然会在KSM守护程序
启动时注册范围，即使该范围不能包含KSM实际可以合并的任何页面，即使MADV_UNMERGEABLE
应用于从未标记为MADV_MERGEABLE的范围。

如果一块内存区域必须被拆分为至少一个新的MADV_MERGEABLE区域或MADV_UNMERGEABLE区域，
当该进程将超过 ``vm.max_map_count`` 的设定，则madvise可能返回ENOMEM。（请参阅文档
Documentation/admin-guide/sysctl/vm.rst）。

与其他madvise调用一样，它们在用户地址空间的映射区域上使用：如果指定的范围包含未
映射的间隙（尽管在中间的映射区域工作），它们将报告ENOMEM，如果没有足够的内存用于
内部结构，则可能会因EAGAIN而失败。

KSM守护进程sysfs接口
====================

KSM守护进程可以由``/sys/kernel/mm/ksm/`` 中的sysfs文件控制，所有人都可以读取，但
只能由root用户写入。各接口解释如下：


pages_to_scan
        ksmd进程进入睡眠前要扫描的页数。
        例如， ``echo 100 > /sys/kernel/mm/ksm/pages_to_scan``

        默认值：100（该值被选择用于演示目的）

sleep_millisecs
        ksmd在下次扫描前应休眠多少毫秒
        例如， ``echo 20 > /sys/kernel/mm/ksm/sleep_millisecs``

        默认值：20（该值被选择用于演示目的）

merge_across_nodes
        指定是否可以合并来自不同NUMA节点的页面。当设置为0时，ksm仅合并在物理上位
        于同一NUMA节点的内存区域中的页面。这降低了访问共享页面的延迟。在有明显的
        NUMA距离上，具有更多节点的系统可能受益于设置该值为0时的更低延迟。而对于
        需要对内存使用量最小化的较小系统来说，设置该值为1（默认设置）则可能会受
        益于更大共享页面。在决定使用哪种设置之前，您可能希望比较系统在每种设置下
        的性能。 ``merge_across_nodes`` 仅当系统中没有ksm共享页面时，才能被更改设
        置：首先将接口`run` 设置为2从而对页进行去合并，然后在修改
        ``merge_across_nodes`` 后再将‘run’又设置为1，以根据新设置来重新合并。

        默认值：1（如早期的发布版本一样合并跨站点）

run
        * 设置为0可停止ksmd运行，但保留合并页面，
        * 设置为1可运行ksmd，例如， ``echo 1 > /sys/kernel/mm/ksm/run`` ，
        * 设置为2可停止ksmd运行，并且对所有目前已合并的页进行去合并，但保留可合并
          区域以供下次运行。

        默认值：0（必须设置为1才能激活KSM，除非禁用了CONFIG_SYSFS）

use_zero_pages
        指定是否应当特殊处理空页（即那些仅含zero的已分配页）。当该值设置为1时，
        空页与内核零页合并，而不是像通常情况下那样空页自身彼此合并。这可以根据
        工作负载的不同，在具有着色零页的架构上可以提高性能。启用此设置时应小心，
        因为它可能会降低某些工作负载的KSM性能，比如，当待合并的候选页面的校验和
        与空页面的校验和恰好匹配的时候。此设置可随时更改，仅对那些更改后再合并
        的页面有效。

        默认值：0（如同早期版本的KSM正常表现）

max_page_sharing
        单个KSM页面允许的最大共享站点数。这将强制执行重复数据消除限制，以避免涉
        及遍历共享KSM页面的虚拟映射的虚拟内存操作的高延迟。最小值为2，因为新创
        建的KSM页面将至少有两个共享者。该值越高，KSM合并内存的速度越快，去重
        因子也越高，但是对于任何给定的KSM页面，虚拟映射的最坏情况遍历的速度也会
        越慢。减慢了这种遍历速度就意味着在交换、压缩、NUMA平衡和页面迁移期间，
        某些虚拟内存操作将有更高的延迟，从而降低这些虚拟内存操作调用者的响应能力。
        其他任务如果不涉及执行虚拟映射遍历的VM操作，其任务调度延迟不受此参数的影
        响，因为这些遍历本身是调度友好的。

stable_node_chains_prune_millisecs
        指定KSM检查特定页面的元数据的频率（即那些达到过时信息数据去重限制标准的
        页面）单位是毫秒。较小的毫秒值将以更低的延迟来释放KSM元数据，但它们将使
        ksmd在扫描期间使用更多CPU。如果还没有一个KSM页面达到 ``max_page_sharing``
        标准，那就没有什么用。

KSM与MADV_MERGEABLE的工作有效性体现于 ``/sys/kernel/mm/ksm/`` 路径下的接口：

pages_shared
        表示多少共享页正在被使用
pages_sharing
        表示还有多少站点正在共享这些共享页，即节省了多少
pages_unshared
        表示有多少页是唯一的，但被反复检查以进行合并
pages_volatile
        表示有多少页因变化太快而无法放在tree中
full_scans
        表示所有可合并区域已扫描多少次
stable_node_chains
        达到 ``max_page_sharing`` 限制的KSM页数
stable_node_dups
        重复的KSM页数

比值 ``pages_sharing/pages_shared`` 的最大值受限制于 ``max_page_sharing``
的设定。要想增加该比值，则相应地要增加 ``max_page_sharing`` 的值。
