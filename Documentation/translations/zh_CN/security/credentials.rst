.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/security/credentials.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

=============
Linux中的凭据
=============

作者: David Howells <dhowells@redhat.com>

.. contents:: :local:

概述
====

当一个对象对另一个对象进行操作时，Linux执行的安全检查包含几个部分：

 1. 对象

     对象是可以直接由用户空间程序操作的系统中的实体。Linux具有多种可操作
     的对象，包括：

	- 任务
	- 文件/索引节点
	- 套接字
	- 消息队列
	- 共享内存段
	- 信号量
	- 密钥

     所有这些对象的描述的一部分是一组凭据。集合中的内容取决于对象的类型。

 2. 对象所有权

     大多数对象的凭据中会有一个子集用来表示该对象的所有权。
     这用于资源核算和限制（如磁盘配额和任务资源限制）。

     例如，在标准的UNIX文件系统中，这将由标记在索引节点上的UID定义。

 3. 对象上下文

     此外在这些对象的凭据中，将有一个子集表示对象的“对象上下文”。
     这可能与（2）中相同，也可能不同 —— 例如，在标准的UNIX文件中，
     这是由标记在索引节点上的UID和GID定义的。

     对象上下文是进行安全计算的一部分，当对象被操作时会用到。

 4. 主体

     主体是正在对其他对象执行操作的对象。

     系统中的大多数对象是不活动的：他们不会对系统中的其他对象起作用。
     进程/任务是明显的例外：它们可以访问和操纵其他对象。

     任务之外的其他对象在某些情况下也可以是主体。例如，打开的文件可以使用
     名为 ``fcntl(F_SETOWN)`` 的任务给它的UID和EUID向一个任务发送SIGIO
     信号。在这种情况下，文件结构也会有一个主体上下文。

 5. 主体上下文

     主体对其凭据有一个额外的解释。其凭据的一个子集形成了“主体上下文”。主体
     上下文在主体执行操作时作为安全计算的一部分使用。

     例如，Linux任务在操作文件时会有FSUID、FSGID和附加组列表 —— 这些凭据
     与通常构成任务的对象上下文的真实UID和GID是相互独立的。

 6. 操作

     Linux提供许多操作，主体可以对对象执行这些操作。可用的操作集取决于主体
     和对象的性质。


     操作包括读取、写入、创建和删除文件，以及派生（forking）或发送
     信号（signalling）和跟踪（tracing）任务等。

 7. 规则，访问控制列表和安全计算

     当主体对对象进行操作时，会进行安全计算。这涉及到使用主体上下文、对象
     上下文和操作，并搜索一个或多个规则集，以确定在给定这些上下文的情况下，
     主体是否被授予或拒绝以所需方式对对象进行操作的权限。

     主要有两个规则来源：

     a. 自主访问控制（DAC）：

	 有时，对象的描述中会包含一组规则。这就是所谓的“访问控制列表”或‘ACL’。
	 一个Linux文件可以提供多个ACL。

	 例如，传统的UNIX文件包括一个权限掩码，它是一个简化的ACL，具有三个固定的
	 主体类别（“用户”、“组”和“其他”），每一个都可以被授予一定的特权（如“读取”、
	 “写入”和“执行” —— 无论这些映射对于对象意味着什么）。然而，UNIX文件权限不
	 允许任意指定主体，因此用途有限。

	 Linux文件还可以支持POSIX ACL。这是一个规则列表，为任意主体授予各种权限。

     b. 强制访问控制（MAC）：

	 整个系统可能有一个或多个规则集，适用于所有主体和对象，不考虑它们的来源。
	 SELinux和Smack就是这种情况的例子。

	 在SELinux和Smack的情况下，每个对象在其凭据中都被赋予一个标签。当请求执
	 行操作时，它们使用主体标签、对象标签和操作，寻找一个规则，该规则表示此操
	 作是授予还是拒绝的。


凭据类型
========

Linux内核支持以下类型的凭据：

 1. 传统的UNIX凭据。

	- 真实用户ID
	- 真实组ID

     UID和GID由大多数（如果不是全部）Linux对象携带，即使有时它们需要被虚构出
     来（例如FAT或CIFS文件，这些文件来源于Windows）。这些（通常）定义了该对象
     的对象上下文，但任务在某些情况下略有不同。

	- 有效用户ID，保存用户ID和FS用户ID
	- 有效组ID，保存组ID和FS组ID
	- 补充组

     这些是仅由任务使用的额外凭据。通常，一个EUID/EGID/GROUPS 被用作主体上下文，
     而真实UID/GID 被用作对象上下文。对于任务，这并不总是正确的。

 2. 能力

	- 允许的能力集合
	- 可继承的能力集合
	- 有效的能力集合
	- 能力边界集合

     这些仅由任务携带，表示授予任务的超出普通任务权限的能力。这些可以通过传统
     UNIX凭据的更改进行隐式操作，但也可以通过 ``capset()`` 系统调用直接操作。

     允许的能力是指进程可以通过 ``capset()`` 将其添加到其有效或允许集合中的
     那些能力。这个可继承的集合也可能受到这样的限制。

     有效能力是任务本身实际可以使用的能力。

     可继承能力是那些可以通过 ``execve()`` 传递的能力。

     边界集限制了通过 ``execve()`` 继承的能力，特别是在以UID 0执行二进制文件时。

 3. 安全管理标记（securebits）

     它们用于控制上述凭据在特定操作如execve()中的操作和继承方式。它们并不直接
     用作对象或主体凭据使用。

 4. 密钥和密钥环

     这些仅由任务携带。它们用于携带和缓存不适合放入其他标准UNIX凭据中的安全令牌。
     它们用诸如使网络文件系统密钥在进程执行的文件访问时可用，而无需让普通程序了解
     涉及的安全细节。

     密钥环是一种特殊类型的密钥。它们携带一组其他密钥，并可以搜索来查找所需的密钥。
     每个进程可以订阅多个密钥环：

	每线程密钥
	每进程密钥环
	每会话密钥环

     当进程访问一个密钥时，若尚不存在，则通常会将其缓存在一个密钥环中，以便将来的
     访问时找到该密钥。

     有关密钥的更多信息，请参见 ``Documentation/translations/zh_CN/security/keys/*`` 。

 5. LSM

     Linux安全模块允许在任务执行操作时施加额外的控制。目前，Linux支持几种LSM选项。

     一些工作通过标记系统中的对象，并应用一组规则（策略）说明某个标签的任务可以对
     另一标签的对象执行哪些操作。

 6. AF_KEY

     这是一种基于套接字网络协议栈中的凭据管理[RFC 2367]。本文档中没有讨论它,因为不
     直接与任务和文件凭据进行交互，而是保留了系统级的凭据。


当打开一个文件时，打开任务的主体上下文的一部分会记录在创建的文件结构中。
这使得使用该文件结构的操作可以使用这些凭据，而不是发出操作的任务的主体上下文。
一个例子是在网络文件系统上打开的文件，打开文件的凭据应该被呈现给服务器，而不管
实际进行读取或写入操作的是谁。


文件标记
========

存储在磁盘上或通过网络获取的文件可能具有注释，构成该文件的对象安全上下文。
根据文件系统的类型，这些注释可能包括以下一项或多项：

 * UNIX UID, GID, mode;
 * Windows user ID;
 * Access control list;
 * LSM security label;
 * UNIX exec privilege escalation bits (SUID/SGID);
 * File capabilities exec privilege escalation bits.

将这些与任务的主体安全上下文进行比较，并根据比较结果允许或禁止执行某些操作。
在execve()的情况下，特权提升位起作用，并且可能允许由可执行文件的注释决定的
进程获得额外的特权。


任务凭据
========

在Linux中，一个任务的所有凭据都保存在一个引用计数结构体‘struct cred’中，
通过(uid, gid)或(groups, keys, LSM security)进行访问。每个任务在其
task_struct中通过一个名为‘cred’的指针指向其凭据。

一旦一组凭据已经准备好并提交，除非以下几种情况，否则不能更改：

 1. 其引用计数可以更改；

 2. 它所指向的 group_info 结构体的引用计数可以更改；

 3. 它所指向的安全数据的引用计数可以更改；

 4. 它所指向的任何密钥环的引用计数可以更改；

 5. 它所指向的任何密钥环可以被撤销、过期或其安全属性可以更改；

 6. 它所指向的任何密钥环的内容可以更改（密钥环的整个目的就是作为一组共享凭据，
    可由具有适当访问权限的任何人修改）。

要更改cred结构体中的任何内容，必须遵循复制和替换的原则。首先进行复制，然后修
改副本，最后使用RCU（读-复制-更新）将任务指针更改为指向新的副本。有一些封装可
用于帮助执行这个过程（见下文）。

一个任务只能修改自己的凭据；不再允许一个任务修改另一个任务的凭据。
这意味着 ``capset()`` 系统调用不再允许使用除当前进程之外的任何PID。
此外， ``keyctl_instantiate()`` 和 ``keyctl_negate()`` 函数也不再
允许在请求进程中附加到特定于进程的密钥环，因为实例化进程可能需要创建它们。


不可变凭据
----------

一旦一组凭据已经被公开（例如通过调用 ``commit_creds()`` ），必须将其视为
不可变的,除了两个例外情况：

 1. 引用计数可以被修改。

 2. 虽然无法更改一组凭据的密钥环订阅，但订阅的密钥环的内容可以被更改。

为了在编译时捕获意外的凭据修改，struct task_struct具有_const_指针指向其凭据集，
struct file也是如此。此外，某些函数如 ``get_cred()`` 和 ``put_cred()`` 在
const指针上操作，因此不需要进行类型转换，但需要临时放弃const限定，以便能够修改
引用计数。


访问任务凭据
------------

任务只能修改自己的凭据，允许当前进程可以读取或替换自己的凭据，无需任何形式锁定的
情况下 —— 这极大简化了事情。它可以调用::

	const struct cred *current_cred()

获取指向其凭据结构的指针，并且之后不必释放它。

有一些方便的封装用于检索任务凭据的特定方面（在每种情况下都只返回值）::

	uid_t current_uid(void)		Current's real UID
	gid_t current_gid(void)		Current's real GID
	uid_t current_euid(void)	Current's effective UID
	gid_t current_egid(void)	Current's effective GID
	uid_t current_fsuid(void)	Current's file access UID
	gid_t current_fsgid(void)	Current's file access GID
	kernel_cap_t current_cap(void)	Current's effective capabilities
	struct user_struct *current_user(void)  Current's user account

还有一些方便的封装，用于检索任务凭据的特定关联对::

	void current_uid_gid(uid_t *, gid_t *);
	void current_euid_egid(uid_t *, gid_t *);
	void current_fsuid_fsgid(uid_t *, gid_t *);

在从当前任务的凭据中检索后，通过其参数返回这些值对。


此外，还有一个函数用于获取当前进程的当前凭据集的引用::

	const struct cred *get_current_cred(void);

以及用于获取对一个实际上不存在于struct cred中的凭据的引用的函数::

	struct user_struct *get_current_user(void);
	struct group_info *get_current_groups(void);

分别获得对当前进程的 user accounting structure 和补充组列表的引用。

一旦获得引用，就必须使用 ``put_cred（）``, ``free_uid（）`` 或
``put_group_info（）`` 来适当释放它。


访问其他任务的凭据
------------------

虽然一个任务可以在不需要锁定的情况下访问自己的凭据，但想要访问另一个任务
的凭据的任务并非如此。它必须使用RCU读锁和 ``rcu_dereference（）``。

``rcu_dereference()`` 是由::

	const struct cred *__task_cred(struct task_struct *task);

这应该在RCU读锁中使用，如下例所示::

	void foo(struct task_struct *t, struct foo_data *f)
	{
		const struct cred *tcred;
		...
		rcu_read_lock();
		tcred = __task_cred(t);
		f->uid = tcred->uid;
		f->gid = tcred->gid;
		f->groups = get_group_info(tcred->groups);
		rcu_read_unlock();
		...
	}

如果需要长时间持有另一个任务的凭据，并且可能在此过程中休眠，则调用方
应该使用以下函数来获取对这些凭据的引用::

	const struct cred *get_task_cred(struct task_struct *task);

这个函数内部完成了所有的RCU操作。当使用完这些凭据时，调用方必须调用put_cred()
函数释放它们。

.. note::
   ``__task_cred()`` 的结果不应直接传递给 ``get_cred()`` ，
   因为这可能与 ``commit_cred()`` 发生竞争条件。

还有一些方便的函数可以访问另一个任务凭据的特定部分，将RCU操作对调用方隐藏起来::

	uid_t task_uid(task)		Task's real UID
	uid_t task_euid(task)		Task's effective UID

如果调用方在此时已经持有RCU读锁，则应使用::

	__task_cred(task)->uid
	__task_cred(task)->euid

类似地，如果需要访问任务凭据的多个方面，应使用RCU读锁，调用 ``__task_cred()``
函数，将结果存储在临时指针中，然后从临时指针中调用凭据的各个方面，最后释放锁。
这样可以防止多次调用昂贵的RCU操作。

如果需要访问另一个任务凭据的其他单个方面，可以使用::

	task_cred_xxx(task, member)

这里的‘member’是cred结构体的非指针成员。例如::

	uid_t task_cred_xxx(task, suid);

将从任务中检索‘struct cred::suid’，并执行适当的RCU操作。对于指针成员，
不能使用这种形式，因为它们指向的内容可能在释放RCU读锁的瞬间消失。


修改凭据
--------

如先前提到的，一个任务只能修改自己的凭据，不能修改其他任务的凭据。这意味
着它不需要使用任何锁来修改自己的凭据。

要修改当前进程的凭据，函数应首先调用::

	struct cred *prepare_creds(void);

这将锁定current->cred_replace_mutex，然后分配并构建当前进程凭据的副本。
如果成功，函数返回时仍然保持互斥锁。如果不成功（内存不足），则返回NULL。

互斥锁防止 ``ptrace()`` 在进行凭据构建和更改的安全检查时更改进程的ptrace
状态，因为ptrace状态可能会改变结果，特别是在 ``execve()`` 的情况下。

新的凭据集应适当地进行修改，并进行任何安全检查和挂钩。在此时，当前和建议的
凭据集都可用，因为current_cred()将返回当前的凭据集。

在替换组列表时，必须在将其添加到凭据之前对新列表进行排序，因为使用二分查找
测试成员资格。实际上，这意味着在set_groups()或set_current_groups()之
前应调用groups_sort()。groups_sort()不能在共享的 ``struct group_list``
上调用，因为即使数组已经排序，它也可能作为排序过程的一部分对元素进行排列。

当凭据集准备好时，应通过调用以下函数将其提交给当前进程::

	int commit_creds(struct cred *new);

这将修改凭据和进程的各个方面，给LSM提供机会做同样的修改，然后使用
``rcu_assign_pointer()`` 将新的凭据实际提交给 ``current->cred`` ，
释放 ``current->cred_replace_mutex`` 以允许 ``ptrace()`` 进行操
作，并通知调度程序和其他组件有关更改的情况。

该函数保证返回0，以便可以在诸如 ``sys_setresuid()`` 函数的末尾进行尾调用。

请注意，该函数会消耗调用者对新凭据的引用。调用者在此之后不应调用
``put_cred()`` 释放新凭据。

此外，一旦新的凭据上调用了该函数，就不能进一步更改这些凭据。


如果在调用 ``prepare_creds()`` 之后安全检查失败或发生其他错误，
则应调用以下函数::

	void abort_creds(struct cred *new);

这将释放 ``prepare_creds()`` 获取的 ``current->cred_replace_mutex`` 的锁，
并释放新的凭据。

一个典型的凭据修改函数看起来像这样::

	int alter_suid(uid_t suid)
	{
		struct cred *new;
		int ret;

		new = prepare_creds();
		if (!new)
			return -ENOMEM;

		new->suid = suid;
		ret = security_alter_suid(new);
		if (ret < 0) {
			abort_creds(new);
			return ret;
		}

		return commit_creds(new);
	}


管理凭据
--------

有一些函数用来辅助凭据管理:

 - ``void put_cred(const struct cred *cred);``

	 这将释放对给定凭据集的引用。如果引用计数为零，凭据集将由
	 RCU系统安排进行销毁。

 - ``const struct cred *get_cred(const struct cred *cred);``

	 这将获取对活动凭据集的引用。返回指向凭据集的指针。

 - ``struct cred *get_new_cred(struct cred *cred);``

	 这将获取对当前正在构建且可变的凭据集的引用。返回指向凭据集的指针。

打开文件凭据
============

当打开新文件时，会获取对打开任务凭据的引用，并将其附加到文件结构体的
``f_cred`` 字段中，替代原来的 ``f_uid`` 和 ``f_gid`` 。原来访问
``file->f_uid`` 和 ``file->f_gid`` 的代码现在应访问 ``file->f_cred->fsuid``
和 ``file->f_cred->fsgid`` 。

安全访问 ``f_cred`` 的情况下可以不使用RCU或加锁，因为指向凭据的指针
以及指向的凭据结构的内容在文件结构的整个生命周期中保持不变，除非是
上述列出的例外情况（参阅任务凭据部分）。

为了避免“混淆代理”权限提升攻击，在打开的文件后续操作时，访问控制检查
应该使用这些凭据，而不是使用“当前”的凭据，因为该文件可能已经被传递给
一个更具特权的进程。

覆盖VFS对凭据的使用
===================

在某些情况下，需要覆盖VFS使用的凭据，可以通过使用不同的凭据集调用
如 ``vfs_mkdir()`` 来实现。以下是一些进行此操作的位置:

 * ``sys_faccessat()``.
 * ``do_coredump()``.
 * nfs4recover.c.
