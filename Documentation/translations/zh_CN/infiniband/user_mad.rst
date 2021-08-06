.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/infiniband/user_mad.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 王普宇 Puyu Wang <realpuyuwang@gmail.com>
 时奎亮 Alex Shi <alexs@kernel.org>

.. _cn_infiniband_user_mad:

===============
用户空间MAD访问
===============

设备文件
========

  每个InfiniBand设备的每个端口都有一个“umad”设备和一个“issm”设备连接。
  例如，一个双端口的HCA将有两个umad设备和两个issm设备，而一个交换机将
  有每个类型的一个设备（对于交换机端口0）。

创建MAD代理
===========

  一个MAD代理可以通过填写一个结构体ib_user_mad_reg_req来创建，然后在
  适当的设备文件的文件描述符上调用IB_USER_MAD_REGISTER_AGENT ioctl。
  如果注册请求成功，结构体中会返回一个32位的ID。比如说::

	struct ib_user_mad_reg_req req = { /* ... */ };
	ret = ioctl(fd, IB_USER_MAD_REGISTER_AGENT, (char *) &req);
        if (!ret)
		my_agent = req.id;
	else
		perror("agent register");

  代理可以通过IB_USER_MAD_UNREGISTER_AGENT ioctl取消注册。另外，所有
  通过文件描述符注册的代理在描述符关闭时将被取消注册。

  2014
       现在提供了一个新的注册IOctl，允许在注册时提供额外的字段。这个注册
       调用的用户隐含了对pkey_index的使用（见下文）。现在提供了一个新的
       注册IOctl，允许在注册时提供额外的字段。这个注册调用的用户隐含了对
       pkey_index的使用（见下文）。

接收MADs
========

  使用read()接收MAD。现在接收端支持RMPP。传给read()的缓冲区必须至少是
  一个struct ib_user_mad + 256字节。比如说:

  如果传递的缓冲区不足以容纳收到的MAD（RMPP），errno被设置为ENOSPC，需
  要的缓冲区长度被设置在mad.length中。

  正常MAD(非RMPP)的读取示例::

	struct ib_user_mad *mad;
	mad = malloc(sizeof *mad + 256);
	ret = read(fd, mad, sizeof *mad + 256);
	if (ret != sizeof mad + 256) {
		perror("read");
		free(mad);
	}

  RMPP读取示例::

	struct ib_user_mad *mad;
	mad = malloc(sizeof *mad + 256);
	ret = read(fd, mad, sizeof *mad + 256);
	if (ret == -ENOSPC)) {
		length = mad.length;
		free(mad);
		mad = malloc(sizeof *mad + length);
		ret = read(fd, mad, sizeof *mad + length);
	}
	if (ret < 0) {
		perror("read");
		free(mad);
	}

  除了实际的MAD内容外，其他结构体ib_user_mad字段将被填入收到的MAD的信
  息。例如，远程LID将在mad.lid中。

  如果发送超时，将产生一个接收，mad.status设置为ETIMEDOUT。否则，当一个
  MAD被成功接收后，mad.status将是0。

  poll()/select()可以用来等待一个MAD可以被读取。

  poll()/select()可以用来等待，直到可以读取一个MAD。

发送MADs
========

  MADs是用write()发送的。发送的代理ID应该填入MAD的id字段，目的地LID应该
  填入lid字段，以此类推。发送端确实支持RMPP，所以可以发送任意长度的MAD。
  比如说::

	struct ib_user_mad *mad;

	mad = malloc(sizeof *mad + mad_length);

	/* fill in mad->data */

	mad->hdr.id  = my_agent;	/* req.id from agent registration */
	mad->hdr.lid = my_dest;		/* in network byte order... */
	/* etc. */

	ret = write(fd, &mad, sizeof *mad + mad_length);
	if (ret != sizeof *mad + mad_length)
		perror("write");

交换IDs
=======

  umad设备的用户可以在发送的MAD中使用交换ID字段的低32位（也就是网络字节顺序中
  最小有效的一半字段）来匹配请求/响应对。上面的32位是保留给内核使用的，在发送
  MAD之前会被改写。

P_Key索引处理
=============

  旧的ib_umad接口不允许为发送的MAD设置P_Key索引，也没有提供获取接收的MAD的
  P_Key索引的方法。一个带有pkey_index成员的struct ib_user_mad_hdr的新布局已
  经被定义；然而，为了保持与旧的应用程序的二进制兼容性，除非在文件描述符被用于
  其他用途之前调用IB_USER_MAD_ENABLE_PKEY或IB_USER_MAD_REGISTER_AGENT2 ioctl
  之一，否则不会使用这种新布局。

  在2008年9月，IB_USER_MAD_ABI_VERSION将被增加到6，默认使用新的ib_user_mad_hdr
  结构布局，并且IB_USER_MAD_ENABLE_PKEY ioctl将被删除。

设置IsSM功能位
==============

  要为一个端口设置IsSM功能位，只需打开相应的issm设备文件。如果IsSM位已经被设置，那
  么打开调用将阻塞，直到该位被清除（或者如果O_NONBLOCK标志被传递给open()，则立即返
  回，errno设置为EAGAIN）。当issm文件被关闭时，IsSM位将被清除。在issm文件上不能进
  行任何读、写或其他操作。

/dev文件
========

为了用 udev自动创建相应的字符设备文件，一个类似::

    KERNEL=="umad*", NAME="infiniband/%k"
    KERNEL=="issm*", NAME="infiniband/%k"

  的规则可以被使用。它将创建节点的名字::

    /dev/infiniband/umad0
    /dev/infiniband/issm0

  为第一个端口，以此类推。与这些设备相关的infiniband设备和端口可以从以下文件中确定::

    /sys/class/infiniband_mad/umad0/ibdev
    /sys/class/infiniband_mad/umad0/port

  和::

    /sys/class/infiniband_mad/issm0/ibdev
    /sys/class/infiniband_mad/issm0/port
