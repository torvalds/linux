.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/filesystems/debugfs.rst

=======
Debugfs
=======

譯者
::

	中文版維護者： 羅楚成 Chucheng Luo <luochucheng@vivo.com>
	中文版翻譯者： 羅楚成 Chucheng Luo <luochucheng@vivo.com>
	中文版校譯者:  羅楚成 Chucheng Luo <luochucheng@vivo.com>
	繁體中文版校譯者: 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>



版權所有2020 羅楚成 <luochucheng@vivo.com>


Debugfs是內核開發人員在用戶空間獲取信息的簡單方法。與/proc不同，proc只提供進程
信息。也不像sysfs,具有嚴格的“每個文件一個值“的規則。debugfs根本沒有規則,開發
人員可以在這裏放置他們想要的任何信息。debugfs文件系統也不能用作穩定的ABI接口。
從理論上講，debugfs導出文件的時候沒有任何約束。但是[1]實際情況並不總是那麼
簡單。即使是debugfs接口，也最好根據需要進行設計,並儘量保持接口不變。


Debugfs通常使用以下命令安裝::

    mount -t debugfs none /sys/kernel/debug

（或等效的/etc/fstab行）。
debugfs根目錄默認僅可由root用戶訪問。要更改對文件樹的訪問，請使用“ uid”，“ gid”
和“ mode”掛載選項。請注意，debugfs API僅按照GPL協議導出到模塊。

使用debugfs的代碼應包含<linux/debugfs.h>。然後，首先是創建至少一個目錄來保存
一組debugfs文件::

    struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);

如果成功，此調用將在指定的父目錄下創建一個名爲name的目錄。如果parent參數爲空，
則會在debugfs根目錄中創建。創建目錄成功時，返回值是一個指向dentry結構體的指針。
該dentry結構體的指針可用於在目錄中創建文件（以及最後將其清理乾淨）。ERR_PTR
（-ERROR）返回值表明出錯。如果返回ERR_PTR（-ENODEV），則表明內核是在沒有debugfs
支持的情況下構建的，並且下述函數都不會起作用。

在debugfs目錄中創建文件的最通用方法是::

    struct dentry *debugfs_create_file(const char *name, umode_t mode,
				       struct dentry *parent, void *data,
				       const struct file_operations *fops);

在這裏，name是要創建的文件的名稱，mode描述了訪問文件應具有的權限，parent指向
應該保存文件的目錄，data將存儲在產生的inode結構體的i_private字段中，而fops是
一組文件操作函數，這些函數中實現文件操作的具體行爲。至少，read（）和/或
write（）操作應提供；其他可以根據需要包括在內。同樣的，返回值將是指向創建文件
的dentry指針，錯誤時返回ERR_PTR（-ERROR），系統不支持debugfs時返回值爲ERR_PTR
（-ENODEV）。創建一個初始大小的文件，可以使用以下函數代替::

    struct dentry *debugfs_create_file_size(const char *name, umode_t mode,
				struct dentry *parent, void *data,
				const struct file_operations *fops,
				loff_t file_size);

file_size是初始文件大小。其他參數跟函數debugfs_create_file的相同。

在許多情況下，沒必要自己去創建一組文件操作;對於一些簡單的情況,debugfs代碼提供
了許多幫助函數。包含單個整數值的文件可以使用以下任何一項創建::

    void debugfs_create_u8(const char *name, umode_t mode,
			   struct dentry *parent, u8 *value);
    void debugfs_create_u16(const char *name, umode_t mode,
			    struct dentry *parent, u16 *value);
    struct dentry *debugfs_create_u32(const char *name, umode_t mode,
				      struct dentry *parent, u32 *value);
    void debugfs_create_u64(const char *name, umode_t mode,
			    struct dentry *parent, u64 *value);

這些文件支持讀取和寫入給定值。如果某個文件不支持寫入，只需根據需要設置mode
參數位。這些文件中的值以十進制表示；如果需要使用十六進制，可以使用以下函數
替代::

    void debugfs_create_x8(const char *name, umode_t mode,
			   struct dentry *parent, u8 *value);
    void debugfs_create_x16(const char *name, umode_t mode,
			    struct dentry *parent, u16 *value);
    void debugfs_create_x32(const char *name, umode_t mode,
			    struct dentry *parent, u32 *value);
    void debugfs_create_x64(const char *name, umode_t mode,
			    struct dentry *parent, u64 *value);

這些功能只有在開發人員知道導出值的大小的時候纔有用。某些數據類型在不同的架構上
有不同的寬度，這樣會使情況變得有些複雜。在這種特殊情況下可以使用以下函數::

    void debugfs_create_size_t(const char *name, umode_t mode,
			       struct dentry *parent, size_t *value);

不出所料，此函數將創建一個debugfs文件來表示類型爲size_t的變量。

同樣地，也有導出無符號長整型變量的函數，分別以十進制和十六進制表示如下::

    struct dentry *debugfs_create_ulong(const char *name, umode_t mode,
					struct dentry *parent,
					unsigned long *value);
    void debugfs_create_xul(const char *name, umode_t mode,
			    struct dentry *parent, unsigned long *value);

布爾值可以通過以下方式放置在debugfs中::

    struct dentry *debugfs_create_bool(const char *name, umode_t mode,
				       struct dentry *parent, bool *value);


讀取結果文件將產生Y（對於非零值）或N，後跟換行符寫入的時候，它只接受大寫或小寫
值或1或0。任何其他輸入將被忽略。

同樣，atomic_t類型的值也可以放置在debugfs中::

    void debugfs_create_atomic_t(const char *name, umode_t mode,
				 struct dentry *parent, atomic_t *value)

讀取此文件將獲得atomic_t值，寫入此文件將設置atomic_t值。

另一個選擇是通過以下結構體和函數導出一個任意二進制數據塊::

    struct debugfs_blob_wrapper {
	void *data;
	unsigned long size;
    };

    struct dentry *debugfs_create_blob(const char *name, umode_t mode,
				       struct dentry *parent,
				       struct debugfs_blob_wrapper *blob);

讀取此文件將返回由指針指向debugfs_blob_wrapper結構體的數據。一些驅動使用“blobs”
作爲一種返回幾行（靜態）格式化文本的簡單方法。這個函數可用於導出二進制信息，但
似乎在主線中沒有任何代碼這樣做。請注意，使用debugfs_create_blob（）命令創建的
所有文件是隻讀的。

如果您要轉儲一個寄存器塊（在開發過程中經常會這麼做，但是這樣的調試代碼很少上傳
到主線中。Debugfs提供兩個函數：一個用於創建僅寄存器文件，另一個把一個寄存器塊
插入一個順序文件中::

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

“base”參數可能爲0，但您可能需要使用__stringify構建reg32數組，實際上有許多寄存器
名稱（宏）是寄存器塊在基址上的字節偏移量。

如果要在debugfs中轉儲u32數組，可以使用以下函數創建文件::

     void debugfs_create_u32_array(const char *name, umode_t mode,
			struct dentry *parent,
			u32 *array, u32 elements);

“array”參數提供數據，而“elements”參數爲數組中元素的數量。注意：數組創建後，數組
大小無法更改。

有一個函數來創建與設備相關的seq_file::

   struct dentry *debugfs_create_devm_seqfile(struct device *dev,
				const char *name,
				struct dentry *parent,
				int (*read_fn)(struct seq_file *s,
					void *data));

“dev”參數是與此debugfs文件相關的設備，並且“read_fn”是一個函數指針，這個函數在
打印seq_file內容的時候被回調。

還有一些其他的面向目錄的函數::

    struct dentry *debugfs_rename(struct dentry *old_dir,
		                  struct dentry *old_dentry,
		                  struct dentry *new_dir,
				  const char *new_name);

    struct dentry *debugfs_create_symlink(const char *name,
                                          struct dentry *parent,
                                          const char *target);

調用debugfs_rename()將爲現有的debugfs文件重命名，可能同時切換目錄。 new_name
函數調用之前不能存在；返回值爲old_dentry，其中包含更新的信息。可以使用
debugfs_create_symlink（）創建符號鏈接。

所有debugfs用戶必須考慮的一件事是：

debugfs不會自動清除在其中創建的任何目錄。如果一個模塊在不顯式刪除debugfs目錄的
情況下卸載模塊，結果將會遺留很多野指針，從而導致系統不穩定。因此，所有debugfs
用戶-至少是那些可以作爲模塊構建的用戶-必須做模塊卸載的時候準備刪除在此創建的
所有文件和目錄。一份文件可以通過以下方式刪除::

    void debugfs_remove(struct dentry *dentry);

dentry值可以爲NULL或錯誤值，在這種情況下，不會有任何文件被刪除。

很久以前，內核開發者使用debugfs時需要記錄他們創建的每個dentry指針，以便最後所有
文件都可以被清理掉。但是，現在debugfs用戶能調用以下函數遞歸清除之前創建的文件::

    void debugfs_remove_recursive(struct dentry *dentry);

如果將對應頂層目錄的dentry傳遞給以上函數，則該目錄下的整個層次結構將會被刪除。

註釋：
[1] http://lwn.net/Articles/309298/

