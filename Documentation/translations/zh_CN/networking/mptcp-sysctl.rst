.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/mptcp-sysctl.rst

:翻译:

   孙渔喜 Sun yuxi <sun.yuxi@zte.com.cn>

================
MPTCP Sysfs 变量
================

/proc/sys/net/mptcp/* Variables
===============================

add_addr_timeout - INTEGER (秒)
	设置ADD_ADDR控制消息的重传超时时间。当MPTCP对端未确认
	先前的ADD_ADDR消息时，将在该超时时间后重新发送。

	默认值与TCP_RTO_MAX相同。此为每个命名空间的sysctl参数。

	默认值：120

allow_join_initial_addr_port - BOOLEAN
	控制是否允许对端向初始子流使用的IP地址和端口号发送加入
	请求（1表示允许）。此参数会设置连接时发送给对端的标志位，
	并决定是否接受此类加入请求。

	通过ADD_ADDR通告的地址不受此参数影响。

	此为每个命名空间的sysctl参数。

	默认值：1

available_path_managers - STRING
	显示已注册的可用路径管理器选项。可能有更多路径管理器可用
	但尚未加载。

available_schedulers - STRING
	显示已注册的可用调度器选项。可能有更多数据包调度器可用
	但尚未加载。

blackhole_timeout - INTEGER (秒)
	当发生MPTCP防火墙黑洞问题时，初始禁用活跃MPTCP套接字上MPTCP
	功能的时间（秒）。如果在重新启用MPTCP后立即检测到更多黑洞问题，
	此时间段将呈指数增长；当黑洞问题消失时，将重置为初始值。

	设置为0可禁用黑洞检测功能。此为每个命名空间的sysctl参数。

	默认值：3600

checksum_enabled - BOOLEAN
	控制是否启用DSS校验和功能。

	当值为非零时可启用DSS校验和。此为每个命名空间的sysctl参数。

	默认值：0

close_timeout - INTEGER (seconds)
	设置"先断后连"超时时间：在未调用close或shutdown系统调用时，
	MPTCP套接字将在最后一个子流移除后保持当前状态达到该时长，才
	会转为TCP_CLOSE状态。

	默认值与TCP_TIMEWAIT_LEN相同。此为每个命名空间的sysctl参数。

	默认值：60

enabled - BOOLEAN
	控制是否允许创建MPTCP套接字。

	当值为1时允许创建MPTCP套接字。此为每个命名空间的sysctl参数。

	默认值：1（启用）

path_manager - STRING
	设置用于每个新MPTCP套接字的默认路径管理器名称。内核路径管理将
	根据通过MPTCP netlink API配置的每个命名空间值来控制子流连接
	和地址通告。用户空间路径管理将每个MPTCP连接的子流连接决策和地
	址通告交由特权用户空间程序控制，代价是需要更多netlink流量来
	传播所有相关事件和命令。

	此为每个命名空间的sysctl参数。

	* "kernel"		  - 内核路径管理器
	* "userspace"	   - 用户空间路径管理器

	默认值："kernel"

pm_type - INTEGER
	设置用于每个新MPTCP套接字的默认路径管理器类型。内核路径管理将
	根据通过MPTCP netlink API配置的每个命名空间值来控制子流连接
	和地址通告。用户空间路径管理将每个MPTCP连接的子流连接决策和地
	址通告交由特权用户空间程序控制，代价是需要更多netlink流量来
	传播所有相关事件和命令。

	此为每个命名空间的sysctl参数。

	自v6.15起已弃用，请改用path_manager参数。

	* 0 - 内核路径管理器
	* 1 - 用户空间路径管理器

	默认值：0

scheduler - STRING
	选择所需的调度器类型。

	支持选择不同的数据包调度器。此为每个命名空间的sysctl参数。

	默认值："default"

stale_loss_cnt - INTEGER
	用于判定子流失效（stale）的MPTCP层重传间隔次数阈值。当指定
	子流在连续多个重传间隔内既无数据传输又有待处理数据时，将被标
	记为失效状态。失效子流将被数据包调度器忽略。
	设置较低的stale_loss_cnt值可实现快速主备切换，较高的值则能
	最大化边缘场景（如高误码率链路或对端暂停数据处理等异常情况）
	的链路利用率。

	此为每个命名空间的sysctl参数。

	默认值：4

syn_retrans_before_tcp_fallback - INTEGER
	在回退到 TCP（即丢弃 MPTCP 选项）之前，SYN + MP_CAPABLE
	报文的重传次数。换句话说，如果所有报文在传输过程中都被丢弃，
	那么将会：

	* 首次SYN携带MPTCP支持选项
	* 按本参数值重传携带MPTCP选项的SYN包
	* 后续重传将不再携带MPTCP支持选项

	0 表示首次重传即丢弃MPTCP选项。
	>=128 表示所有SYN重传均保留MPTCP选项设置过低的值可能增加
	MPTCP黑洞误判几率。此为每个命名空间的sysctl参数。

	默认值：2
