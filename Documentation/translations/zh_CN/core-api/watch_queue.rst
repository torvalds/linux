.. SPDX-License-Identifier: GPL-2.0+

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/watch_queue.rst

:翻译:

周彬彬 Binbin Zhou <zhoubinbin@loongson.cn>

:校译:

司延腾 Yanteng Si <siyanteng@loongson.cn>
吴想成 Wu Xiangcheng <bobwxc@email.cn>


============
通用通知机制
============

通用通知机制是建立在标准管道驱动之上的，它可以有效地将来自内核的通知消息拼接到用
户空间打开的管道中。这可以与以下方面结合使用::

  * Key/keyring 通知

通知缓冲区可以通过以下方式启用：

	“General setup”/“General notification queue”
	(CONFIG_WATCH_QUEUE)

文档包含以下章节：

.. contents:: :local:


概述
====

该设施以一种特殊模式打开的管道形式出现，管道的内部环形缓冲区用于保存内核生成的消
息。然后通过read()读出这些消息。在此类管道上禁用拼接以及类似的操作，因为它们希望
在某些情况下将其添加的内容还原到环中-这可能最终会与通知消息重叠。

管道的所有者必须告诉内核它想通过该管道观察哪些源。只有连接到该管道上的源才会将消
息插入其中。请注意，一个源可能绑定到多个管道，并同时将消息插入到所有管道中。

还可以将过滤器放置在管道上，以便在不感兴趣时可以忽略某些源类型和子事件。

如果环中没有可用的插槽，或者没有预分配的消息缓冲区可用，则将丢弃消息。在这两种情
况下，read()都会在读取缓冲区中当前的最后一条消息后，将WATCH_META_LOSS_NOTIFICATION
插入到输出缓冲区中。

请注意，当生成一个通知时，内核不会等待消费者收集它，而是继续执行。这意味着可以在
持有自旋锁的同时生成通知，并且还可以保护内核不被用户空间故障无限期地阻碍。


消息结构
========

通知消息由一个简短的头部开始::

	struct watch_notification {
		__u32	type:24;
		__u32	subtype:8;
		__u32	info;
	};

“type”表示通知记录的来源，“subtype”表示该来源的记录类型（见下文观测源章节）。该类
型也可以是“WATCH_TYPE_META”。这是一个由观测队列本身在内部生成的特殊记录类型。有两
个子类型：

  * WATCH_META_REMOVAL_NOTIFICATION
  * WATCH_META_LOSS_NOTIFICATION

第一个表示安装了观察的对象已被删除或销毁，第二个表示某些消息已丢失。

“info”表示一系列东西，包括：

  * 消息的长度，以字节为单位，包括头（带有WATCH_INFO_LENGTH的掩码，并按
    WATCH_INFO_LENGTH__SHIFT移位）。这表示记录的大小，可能在8到127字节之间。

  * 观测ID（带有WATCH_INFO_ID掩码，并按WATCH_INFO_ID__SHIFT移位）。这表示观测的主
    叫ID，可能在0到255之间。多个观测组可以共享一个队列，这提供了一种区分它们的方法。

  * 特定类型的字段（WATCH_INFO_TYPE_INFO）。这是由通知生产者设置的，以指示类型和
    子类型的某些特定含义。

除长度外，信息中的所有内容都可以用于过滤。

头部后面可以有补充信息。此格式是由类型和子类型决定的。


观测列表（通知源）API
=====================

“观测列表“是订阅通知源的观测者的列表。列表可以附加到对象（比如键或超级块），也可
以是全局的（比如对于设备事件）。从用户空间的角度来看，一个非全局的观测列表通常是
通过引用它所属的对象来引用的（比如使用KEYCTL_NOTIFY并给它一个密钥序列号来观测特定
的密钥）。

为了管理观测列表，提供了以下函数：

  * ::

	void init_watch_list(struct watch_list *wlist,
			     void (*release_watch)(struct watch *wlist));

    初始化一个观测列表。 如果 ``release_watch`` 不是NULL，那么这表示当watch_list对
    象被销毁时，应该调用函数来丢弃观测列表对被观测对象的任何引用。

  * ``void remove_watch_list(struct watch_list *wlist);``

    这将删除订阅watch_list的所有观测，并释放它们，然后销毁watch_list对象本身。


观测队列（通知输出）API
=======================

“观测队列”是由应用程序分配的用以记录通知的缓冲区，其工作原理完全隐藏在管道设备驱
动中，但必须获得对它的引用才能设置观测。可以通过以下方式进行管理：

  * ``struct watch_queue *get_watch_queue(int fd);``

    由于观测队列在内核中通过实现缓冲区的管道的文件描述符表示，用户空间必须通过系
    统调用传递该文件描述符，这可以用于从系统调用中查找指向观测队列的不透明指针。

  * ``void put_watch_queue(struct watch_queue *wqueue);``

    该函数用以丢弃从 ``get_watch_queue()`` 获得的引用。


观测订阅API
===========

“观测”是观测列表上的订阅，表示观测队列，从而表示应写入通知记录的缓冲区。观测队列
对象还可以携带该对象的过滤规则，由用户空间设置。watch结构体的某些部分可以由驱动程
序设置::

	struct watch {
		union {
			u32		info_id;	/* 在info字段中进行OR运算的ID */
			...
		};
		void			*private;	/* 被观测对象的私有数据 */
		u64			id;		/* 内部标识符 */
		...
	};

``info_id`` 值是从用户空间获得并按WATCH_INFO_ID__SHIFT移位的8位数字。当通知写入关
联的观测队列缓冲区时，这将与struct watch_notification::info的WATCH_INFO_ID字段进
行或运算。

``private`` 字段是与watch_list相关联的驱动程序数据，并由 ``watch_list::release_watch()``
函数清除。

``id`` 字段是源的ID。使用不同ID发布的通知将被忽略。

提供以下函数来管理观测:

  * ``void init_watch(struct watch *watch, struct watch_queue *wqueue);``

    初始化一个观测对象，把它的指针设置到观察队列中，使用适当的限制来避免死锁。

  * ``int add_watch_to_object(struct watch *watch, struct watch_list *wlist);``

    将观测订阅到观测列表（通知源）。watch结构体中的driver-settable字段必须在调用
    它之前设置。

  * ::

	int remove_watch_from_object(struct watch_list *wlist,
				     struct watch_queue *wqueue,
				     u64 id, false);

    从观测列表中删除一个观测，该观测必须与指定的观测队列（``wqueue``）和对象标识
    符（``id``）匹配。通知（``WATCH_META_REMOVAL_NOTIFICATION``）被发送到观测队列
    表示该观测已被删除。

  * ``int remove_watch_from_object(struct watch_list *wlist, NULL, 0, true);``

    从观测列表中删除所有观测。预计这将被称为销毁前的准备工作，届时新的观测将无法
    访问观测列表。通知（``WATCH_META_REMOVAL_NOTIFICATION``）被发送到每个订阅观测
    的观测队列，以表明该观测已被删除。


通知发布API
===========

要将通知发布到观测列表以便订阅的观测可以看到，应使用以下函数::

	void post_watch_notification(struct watch_list *wlist,
				     struct watch_notification *n,
				     const struct cred *cred,
				     u64 id);

应预先设置通知格式，并应传入一个指向头部（``n``）的指针。通知可能大于此值，并且缓
冲槽为单位的大小在 ``n->info & WATCH_INFO_LENGTH`` 中注明。

``cred`` 结构体表示源（对象）的证书，并传递给LSM，例如SELinux，以允许或禁止根据该队
列（对象）的证书在每个单独队列中记录注释。

``id`` 是源对象ID（如密钥上的序列号）。只有设置相同ID的观测才能看到这个通知。


观测源
======

任何特定的缓冲区都可以从多个源获取信息。 这些源包括:

  * WATCH_TYPE_KEY_NOTIFY

    这种类型的通知表示密钥和密钥环的变化，包括密钥环内容或密钥属性的变化。

    更多信息请参见Documentation/security/keys/core.rst。


事件过滤
========

当创建观测队列后，我们可以应用一组过滤器以限制接收的事件::

	struct watch_notification_filter filter = {
		...
	};
	ioctl(fd, IOC_WATCH_QUEUE_SET_FILTER, &filter)

过滤器的描述的类型变量是::

	struct watch_notification_filter {
		__u32	nr_filters;
		__u32	__reserved;
		struct watch_notification_type_filter filters[];
	};

其中“nr_filters”表示filters[]数组中过滤器的数量，而“__reserved”应为0。
“filter”数组有以下类型的元素::

	struct watch_notification_type_filter {
		__u32	type;
		__u32	info_filter;
		__u32	info_mask;
		__u32	subtype_filter[8];
	};

其中：

  * ``type`` 是过滤的事件类型，应类似于“WATCH_TYPE_KEY_NOTIFY”。

  * ``info_filter`` 与 ``info_mask`` 充当通知记录的信息字段的过滤器，只有在以下情
    况，通知才会写入缓冲区::

	(watch.info & info_mask) == info_filter

    例如，这可以用于忽略不在一个挂载树上的观测点的事件。

  * ``subtype_filter`` 是一个位掩码，表示感兴趣的子类型。subtype_filter[0]的
    bit[0]对应子类型0，bit[1]对应子类型1，以此类推。

若ioctl()的参数为NULL，则过滤器将被移除，并且来自观测源的所有事件都将通过。


用户空间代码示例
================

缓冲区的创建如下所示::

	pipe2(fds, O_TMPFILE);
	ioctl(fds[1], IOC_WATCH_QUEUE_SET_SIZE, 256);

它可以被设置成接收密钥环变化的通知::

	keyctl(KEYCTL_WATCH_KEY, KEY_SPEC_SESSION_KEYRING, fds[1], 0x01);

然后，这些通知可以被如下方式所使用::

	static void consumer(int rfd, struct watch_queue_buffer *buf)
	{
		unsigned char buffer[128];
		ssize_t buf_len;

		while (buf_len = read(rfd, buffer, sizeof(buffer)),
		       buf_len > 0
		       ) {
			void *p = buffer;
			void *end = buffer + buf_len;
			while (p < end) {
				union {
					struct watch_notification n;
					unsigned char buf1[128];
				} n;
				size_t largest, len;

				largest = end - p;
				if (largest > 128)
					largest = 128;
				memcpy(&n, p, largest);

				len = (n->info & WATCH_INFO_LENGTH) >>
					WATCH_INFO_LENGTH__SHIFT;
				if (len == 0 || len > largest)
					return;

				switch (n.n.type) {
				case WATCH_TYPE_META:
					got_meta(&n.n);
				case WATCH_TYPE_KEY_NOTIFY:
					saw_key_change(&n.n);
					break;
				}

				p += len;
			}
		}
	}
