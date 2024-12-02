.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/kref.rst

翻译:

司延腾 Yanteng Si <siyanteng@loongson.cn>

校译：

 <此处请校译员签名（自愿），我将在下一个版本添加>

.. _cn_core_api_kref.rst:

=================================
为内核对象添加引用计数器（krefs）
=================================

:作者: Corey Minyard <minyard@acm.org>
:作者: Thomas Hellstrom <thellstrom@vmware.com>

其中很多内容都是从Greg Kroah-Hartman2004年关于krefs的OLS论文和演讲中摘
录的，可以在以下网址找到:

  - http://www.kroah.com/linux/talks/ols_2004_kref_paper/Reprint-Kroah-Hartman-OLS2004.pdf
  - http://www.kroah.com/linux/talks/ols_2004_kref_talk/

简介
====

krefs允许你为你的对象添加引用计数器。如果你有在多个地方使用和传递的对象，
而你没有refcounts，你的代码几乎肯定是坏的。如果你想要引用计数，krefs是个
好办法。

要使用kref，请在你的数据结构中添加一个，如::

    struct my_data
    {
	.
	.
	struct kref refcount;
	.
	.
    };

kref可以出现在数据结构体中的任何地方。

初始化
======

你必须在分配kref之后初始化它。 要做到这一点，可以这样调用kref_init::

     struct my_data *data;

     data = kmalloc(sizeof(*data), GFP_KERNEL);
     if (!data)
            return -ENOMEM;
     kref_init(&data->refcount);

这将kref中的refcount设置为1。

Kref规则
========

一旦你有一个初始化的kref，你必须遵循以下规则:

1) 如果你对一个指针做了一个非临时性的拷贝，特别是如果它可以被传递给另一个执
   行线程，你必须在传递之前用kref_get()增加refcount::

       kref_get(&data->refcount);

	如果你已经有了一个指向kref-ed结构体的有效指针（refcount不能为零），你
	可以在没有锁的情况下这样做。

2) 当你完成对一个指针的处理时，你必须调用kref_put()::

       kref_put(&data->refcount, data_release);

   如果这是对该指针的最后一次引用，释放程序将被调用。如果代码从来没有尝试过
   在没有已经持有有效指针的情况下获得一个kref-ed结构体的有效指针，那么在没
   有锁的情况下这样做是安全的。

3) 如果代码试图获得对一个kref-ed结构体的引用，而不持有一个有效的指针，它必
   须按顺序访问，在kref_put()期间不能发生kref_get()，并且该结构体在kref_get()
   期间必须保持有效。

例如，如果你分配了一些数据，然后将其传递给另一个线程来处理::

    void data_release(struct kref *ref)
    {
	struct my_data *data = container_of(ref, struct my_data, refcount);
	kfree(data);
    }

    void more_data_handling(void *cb_data)
    {
	struct my_data *data = cb_data;
	.
	. do stuff with data here
	.
	kref_put(&data->refcount, data_release);
    }

    int my_data_handler(void)
    {
	int rv = 0;
	struct my_data *data;
	struct task_struct *task;
	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	kref_init(&data->refcount);

	kref_get(&data->refcount);
	task = kthread_run(more_data_handling, data, "more_data_handling");
	if (task == ERR_PTR(-ENOMEM)) {
		rv = -ENOMEM;
	        kref_put(&data->refcount, data_release);
		goto out;
	}

	.
	. do stuff with data here
	.
    out:
	kref_put(&data->refcount, data_release);
	return rv;
    }

这样，两个线程处理数据的顺序并不重要，kref_put()处理知道数据不再被引用并释
放它。kref_get()不需要锁，因为我们已经有了一个有效的指针，我们拥有一个
refcount。put不需要锁，因为没有任何东西试图在没有持有指针的情况下获取数据。

在上面的例子中，kref_put()在成功和错误路径中都会被调用2次。这是必要的，因
为引用计数被kref_init()和kref_get()递增了2次。

请注意，规则1中的 "before "是非常重要的。你不应该做类似于::

	task = kthread_run(more_data_handling, data, "more_data_handling");
	if (task == ERR_PTR(-ENOMEM)) {
		rv = -ENOMEM;
		goto out;
	} else
		/* BAD BAD BAD - 在交接后得到 */
		kref_get(&data->refcount);

不要以为你知道自己在做什么而使用上述构造。首先，你可能不知道自己在做什么。
其次，你可能知道自己在做什么（有些情况下涉及到锁，上述做法可能是合法的），
但其他不知道自己在做什么的人可能会改变代码或复制代码。这是很危险的作风。请
不要这样做。

在有些情况下，你可以优化get和put。例如，如果你已经完成了一个对象，并且给其
他对象排队，或者把它传递给其他对象，那么就没有理由先做一个get，然后再做一个
put::

	/* 糟糕的额外获取(get)和输出(put) */
	kref_get(&obj->ref);
	enqueue(obj);
	kref_put(&obj->ref, obj_cleanup);

只要做enqueue就可以了。 我们随时欢迎对这个问题的评论::

	enqueue(obj);
	/* 我们已经完成了对obj的处理，所以我们把我们的refcount传给了队列。
	 在这之后不要再碰obj了! */

最后一条规则（规则3）是最难处理的一条。例如，你有一个每个项目都被krefed的列表，
而你希望得到第一个项目。你不能只是从列表中抽出第一个项目，然后kref_get()它。
这违反了规则3，因为你还没有持有一个有效的指针。你必须添加一个mutex（或其他锁）。
比如说::

	static DEFINE_MUTEX(mutex);
	static LIST_HEAD(q);
	struct my_data
	{
		struct kref      refcount;
		struct list_head link;
	};

	static struct my_data *get_entry()
	{
		struct my_data *entry = NULL;
		mutex_lock(&mutex);
		if (!list_empty(&q)) {
			entry = container_of(q.next, struct my_data, link);
			kref_get(&entry->refcount);
		}
		mutex_unlock(&mutex);
		return entry;
	}

	static void release_entry(struct kref *ref)
	{
		struct my_data *entry = container_of(ref, struct my_data, refcount);

		list_del(&entry->link);
		kfree(entry);
	}

	static void put_entry(struct my_data *entry)
	{
		mutex_lock(&mutex);
		kref_put(&entry->refcount, release_entry);
		mutex_unlock(&mutex);
	}

如果你不想在整个释放操作过程中持有锁，kref_put()的返回值是有用的。假设你不想在
上面的例子中在持有锁的情况下调用kfree()（因为这样做有点无意义）。你可以使用kref_put()，
如下所示::

	static void release_entry(struct kref *ref)
	{
		/* 所有的工作都是在从kref_put()返回后完成的。*/
	}

	static void put_entry(struct my_data *entry)
	{
		mutex_lock(&mutex);
		if (kref_put(&entry->refcount, release_entry)) {
			list_del(&entry->link);
			mutex_unlock(&mutex);
			kfree(entry);
		} else
			mutex_unlock(&mutex);
	}

如果你必须调用其他程序作为释放操作的一部分，而这些程序可能需要很长的时间，或者可
能要求相同的锁，那么这真的更有用。请注意，在释放例程中做所有的事情还是比较好的，
因为它比较整洁。

上面的例子也可以用kref_get_unless_zero()来优化，方法如下::

	static struct my_data *get_entry()
	{
		struct my_data *entry = NULL;
		mutex_lock(&mutex);
		if (!list_empty(&q)) {
			entry = container_of(q.next, struct my_data, link);
			if (!kref_get_unless_zero(&entry->refcount))
				entry = NULL;
		}
		mutex_unlock(&mutex);
		return entry;
	}

	static void release_entry(struct kref *ref)
	{
		struct my_data *entry = container_of(ref, struct my_data, refcount);

		mutex_lock(&mutex);
		list_del(&entry->link);
		mutex_unlock(&mutex);
		kfree(entry);
	}

	static void put_entry(struct my_data *entry)
	{
		kref_put(&entry->refcount, release_entry);
	}

这对于在put_entry()中移除kref_put()周围的mutex锁是很有用的，但是重要的是
kref_get_unless_zero被封装在查找表中的同一关键部分，否则kref_get_unless_zero
可能引用已经释放的内存。注意，在不检查其返回值的情况下使用kref_get_unless_zero
是非法的。如果你确信（已经有了一个有效的指针）kref_get_unless_zero()会返回true，
那么就用kref_get()代替。

Krefs和RCU
==========

函数kref_get_unless_zero也使得在上述例子中使用rcu锁进行查找成为可能::

	struct my_data
	{
		struct rcu_head rhead;
		.
		struct kref refcount;
		.
		.
	};

	static struct my_data *get_entry_rcu()
	{
		struct my_data *entry = NULL;
		rcu_read_lock();
		if (!list_empty(&q)) {
			entry = container_of(q.next, struct my_data, link);
			if (!kref_get_unless_zero(&entry->refcount))
				entry = NULL;
		}
		rcu_read_unlock();
		return entry;
	}

	static void release_entry_rcu(struct kref *ref)
	{
		struct my_data *entry = container_of(ref, struct my_data, refcount);

		mutex_lock(&mutex);
		list_del_rcu(&entry->link);
		mutex_unlock(&mutex);
		kfree_rcu(entry, rhead);
	}

	static void put_entry(struct my_data *entry)
	{
		kref_put(&entry->refcount, release_entry_rcu);
	}

但要注意的是，在调用release_entry_rcu后，结构kref成员需要在有效内存中保留一个rcu
宽限期。这可以通过使用上面的kfree_rcu(entry, rhead)来实现，或者在使用kfree之前
调用synchronize_rcu()，但注意synchronize_rcu()可能会睡眠相当长的时间。
