.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/filesystems/debugfs.rst

=======
Debugfs
=======

译者
::

	中文版维护者： 罗楚成 Chucheng Luo <luochucheng@vivo.com>
	中文版翻译者： 罗楚成 Chucheng Luo <luochucheng@vivo.com>
	中文版校译者:  罗楚成 Chucheng Luo <luochucheng@vivo.com>



版权所有2020 罗楚成 <luochucheng@vivo.com>


Debugfs是内核开发人员在用户空间获取信息的简单方法。与/proc不同，proc只提供进程
信息。也不像sysfs,具有严格的“每个文件一个值“的规则。debugfs根本没有规则,开发
人员可以在这里放置他们想要的任何信息。debugfs文件系统也不能用作稳定的ABI接口。
从理论上讲，debugfs导出文件的时候没有任何约束。但是[1]实际情况并不总是那么
简单。即使是debugfs接口，也最好根据需要进行设计,并尽量保持接口不变。


Debugfs通常使用以下命令安装::

    mount -t debugfs none /sys/kernel/debug

（或等效的/etc/fstab行）。
debugfs根目录默认仅可由root用户访问。要更改对文件树的访问，请使用“ uid”，“ gid”
和“ mode”挂载选项。请注意，debugfs API仅按照GPL协议导出到模块。

使用debugfs的代码应包含<linux/debugfs.h>。然后，首先是创建至少一个目录来保存
一组debugfs文件::

    struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);

如果成功，此调用将在指定的父目录下创建一个名为name的目录。如果parent参数为空，
则会在debugfs根目录中创建。创建目录成功时，返回值是一个指向dentry结构体的指针。
该dentry结构体的指针可用于在目录中创建文件（以及最后将其清理干净）。ERR_PTR
（-ERROR）返回值表明出错。如果返回ERR_PTR（-ENODEV），则表明内核是在没有debugfs
支持的情况下构建的，并且下述函数都不会起作用。

在debugfs目录中创建文件的最通用方法是::

    struct dentry *debugfs_create_file(const char *name, umode_t mode,
				       struct dentry *parent, void *data,
				       const struct file_operations *fops);

在这里，name是要创建的文件的名称，mode描述了访问文件应具有的权限，parent指向
应该保存文件的目录，data将存储在产生的inode结构体的i_private字段中，而fops是
一组文件操作函数，这些函数中实现文件操作的具体行为。至少，read（）和/或
write（）操作应提供；其他可以根据需要包括在内。同样的，返回值将是指向创建文件
的dentry指针，错误时返回ERR_PTR（-ERROR），系统不支持debugfs时返回值为ERR_PTR
（-ENODEV）。创建一个初始大小的文件，可以使用以下函数代替::

    struct dentry *debugfs_create_file_size(const char *name, umode_t mode,
				struct dentry *parent, void *data,
				const struct file_operations *fops,
				loff_t file_size);

file_size是初始文件大小。其他参数跟函数debugfs_create_file的相同。

在许多情况下，没必要自己去创建一组文件操作;对于一些简单的情况,debugfs代码提供
了许多帮助函数。包含单个整数值的文件可以使用以下任何一项创建::

    void debugfs_create_u8(const char *name, umode_t mode,
			   struct dentry *parent, u8 *value);
    void debugfs_create_u16(const char *name, umode_t mode,
			    struct dentry *parent, u16 *value);
    struct dentry *debugfs_create_u32(const char *name, umode_t mode,
				      struct dentry *parent, u32 *value);
    void debugfs_create_u64(const char *name, umode_t mode,
			    struct dentry *parent, u64 *value);

这些文件支持读取和写入给定值。如果某个文件不支持写入，只需根据需要设置mode
参数位。这些文件中的值以十进制表示；如果需要使用十六进制，可以使用以下函数
替代::

    void debugfs_create_x8(const char *name, umode_t mode,
			   struct dentry *parent, u8 *value);
    void debugfs_create_x16(const char *name, umode_t mode,
			    struct dentry *parent, u16 *value);
    void debugfs_create_x32(const char *name, umode_t mode,
			    struct dentry *parent, u32 *value);
    void debugfs_create_x64(const char *name, umode_t mode,
			    struct dentry *parent, u64 *value);

这些功能只有在开发人员知道导出值的大小的时候才有用。某些数据类型在不同的架构上
有不同的宽度，这样会使情况变得有些复杂。在这种特殊情况下可以使用以下函数::

    void debugfs_create_size_t(const char *name, umode_t mode,
			       struct dentry *parent, size_t *value);

不出所料，此函数将创建一个debugfs文件来表示类型为size_t的变量。

同样地，也有导出无符号长整型变量的函数，分别以十进制和十六进制表示如下::

    struct dentry *debugfs_create_ulong(const char *name, umode_t mode,
					struct dentry *parent,
					unsigned long *value);
    void debugfs_create_xul(const char *name, umode_t mode,
			    struct dentry *parent, unsigned long *value);

布尔值可以通过以下方式放置在debugfs中::

    struct dentry *debugfs_create_bool(const char *name, umode_t mode,
				       struct dentry *parent, bool *value);


读取结果文件将产生Y（对于非零值）或N，后跟换行符写入的时候，它只接受大写或小写
值或1或0。任何其他输入将被忽略。

同样，atomic_t类型的值也可以放置在debugfs中::

    void debugfs_create_atomic_t(const char *name, umode_t mode,
				 struct dentry *parent, atomic_t *value)

读取此文件将获得atomic_t值，写入此文件将设置atomic_t值。

另一个选择是通过以下结构体和函数导出一个任意二进制数据块::

    struct debugfs_blob_wrapper {
	void *data;
	unsigned long size;
    };

    struct dentry *debugfs_create_blob(const char *name, umode_t mode,
				       struct dentry *parent,
				       struct debugfs_blob_wrapper *blob);

读取此文件将返回由指针指向debugfs_blob_wrapper结构体的数据。一些驱动使用“blobs”
作为一种返回几行（静态）格式化文本的简单方法。这个函数可用于导出二进制信息，但
似乎在主线中没有任何代码这样做。请注意，使用debugfs_create_blob（）命令创建的
所有文件是只读的。

如果您要转储一个寄存器块（在开发过程中经常会这么做，但是这样的调试代码很少上传
到主线中。Debugfs提供两个函数：一个用于创建仅寄存器文件，另一个把一个寄存器块
插入一个顺序文件中::

    struct debugfs_reg32 {
	char *name;
	unsigned long offset;
    };

    struct debugfs_regset32 {
	struct debugfs_reg32 *regs;
	int nregs;
	void __iomem *base;
    };

    struct dentry *debugfs_create_regset32(const char *name, umode_t mode,
				     struct dentry *parent,
				     struct debugfs_regset32 *regset);

    void debugfs_print_regs32(struct seq_file *s, struct debugfs_reg32 *regs,
			 int nregs, void __iomem *base, char *prefix);

“base”参数可能为0，但您可能需要使用__stringify构建reg32数组，实际上有许多寄存器
名称（宏）是寄存器块在基址上的字节偏移量。

如果要在debugfs中转储u32数组，可以使用以下函数创建文件::

     void debugfs_create_u32_array(const char *name, umode_t mode,
			struct dentry *parent,
			u32 *array, u32 elements);

“array”参数提供数据，而“elements”参数为数组中元素的数量。注意：数组创建后，数组
大小无法更改。

有一个函数来创建与设备相关的seq_file::

   struct dentry *debugfs_create_devm_seqfile(struct device *dev,
				const char *name,
				struct dentry *parent,
				int (*read_fn)(struct seq_file *s,
					void *data));

“dev”参数是与此debugfs文件相关的设备，并且“read_fn”是一个函数指针，这个函数在
打印seq_file内容的时候被回调。

还有一些其他的面向目录的函数::

    struct dentry *debugfs_rename(struct dentry *old_dir,
		                  struct dentry *old_dentry,
		                  struct dentry *new_dir,
				  const char *new_name);

    struct dentry *debugfs_create_symlink(const char *name,
                                          struct dentry *parent,
                                          const char *target);

调用debugfs_rename()将为现有的debugfs文件重命名，可能同时切换目录。 new_name
函数调用之前不能存在；返回值为old_dentry，其中包含更新的信息。可以使用
debugfs_create_symlink（）创建符号链接。

所有debugfs用户必须考虑的一件事是：

debugfs不会自动清除在其中创建的任何目录。如果一个模块在不显式删除debugfs目录的
情况下卸载模块，结果将会遗留很多野指针，从而导致系统不稳定。因此，所有debugfs
用户-至少是那些可以作为模块构建的用户-必须做模块卸载的时候准备删除在此创建的
所有文件和目录。一份文件可以通过以下方式删除::

    void debugfs_remove(struct dentry *dentry);

dentry值可以为NULL或错误值，在这种情况下，不会有任何文件被删除。

很久以前，内核开发者使用debugfs时需要记录他们创建的每个dentry指针，以便最后所有
文件都可以被清理掉。但是，现在debugfs用户能调用以下函数递归清除之前创建的文件::

    void debugfs_remove_recursive(struct dentry *dentry);

如果将对应顶层目录的dentry传递给以上函数，则该目录下的整个层次结构将会被删除。

注释：
[1] http://lwn.net/Articles/309298/
