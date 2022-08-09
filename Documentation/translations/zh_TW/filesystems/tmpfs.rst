.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/filesystems/tmpfs.rst

Translated by Wang Qing <wangqing@vivo.com>
and Hu Haowen <src.res@email.cn>

=====
Tmpfs
=====

Tmpfs是一個將所有文件都保存在虛擬內存中的文件系統。

tmpfs中的所有內容都是臨時的，也就是說沒有任何文件會在硬碟上創建。
如果卸載tmpfs實例，所有保存在其中的文件都會丟失。

tmpfs將所有文件保存在內核緩存中，隨著文件內容增長或縮小可以將不需要的
頁面swap出去。它具有最大限制，可以通過「mount -o remount ...」調整。

和ramfs（創建tmpfs的模板）相比，tmpfs包含交換和限制檢查。和tmpfs相似的另
一個東西是RAM磁碟（/dev/ram*），可以在物理RAM中模擬固定大小的硬碟，並在
此之上創建一個普通的文件系統。Ramdisks無法swap，因此無法調整它們的大小。

由於tmpfs完全保存於頁面緩存和swap中，因此所有tmpfs頁面將在/proc/meminfo
中顯示爲「Shmem」，而在free(1)中顯示爲「Shared」。請注意，這些計數還包括
共享內存(shmem，請參閱ipcs(1))。獲得計數的最可靠方法是使用df(1)和du(1)。

tmpfs具有以下用途：

1) 內核總有一個無法看到的內部掛載，用於共享匿名映射和SYSV共享內存。

   掛載不依賴於CONFIG_TMPFS。如果CONFIG_TMPFS未設置，tmpfs對用戶不可見。
   但是內部機制始終存在。

2) glibc 2.2及更高版本期望將tmpfs掛載在/dev/shm上以用於POSIX共享內存
   (shm_open，shm_unlink)。添加內容到/etc/fstab應注意如下：

	tmpfs	/dev/shm	tmpfs	defaults	0 0

   使用時需要記住創建掛載tmpfs的目錄。

   SYSV共享內存無需掛載，內部已默認支持。(在2.3內核版本中，必須掛載
   tmpfs的前身(shm fs)才能使用SYSV共享內存)

3) 很多人（包括我）都覺的在/tmp和/var/tmp上掛載非常方便，並具有較大的
   swap分區。目前循環掛載tmpfs可以正常工作，所以大多數發布都應當可以
   使用mkinitrd通過/tmp訪問/tmp。

4) 也許還有更多我不知道的地方:-)


tmpfs有三個用於調整大小的掛載選項：

=========  ===========================================================
size       tmpfs實例分配的字節數限制。默認值是不swap時物理RAM的一半。
           如果tmpfs實例過大，機器將死鎖，因爲OOM處理將無法釋放該內存。
nr_blocks  與size相同，但以PAGE_SIZE爲單位。
nr_inodes  tmpfs實例的最大inode個數。默認值是物理內存頁數的一半，或者
           (有高端內存的機器)低端內存RAM的頁數，二者以較低者為準。
=========  ===========================================================

這些參數接受後綴k，m或g表示千，兆和千兆字節，可以在remount時更改。
size參數也接受後綴％用來限制tmpfs實例占用物理RAM的百分比：
未指定size或nr_blocks時，默認值爲size=50％

如果nr_blocks=0（或size=0），block個數將不受限制；如果nr_inodes=0，
inode個數將不受限制。這樣掛載通常是不明智的，因爲它允許任何具有寫權限的
用戶通過訪問tmpfs耗盡機器上的所有內存；但同時這樣做也會增強在多個CPU的
場景下的訪問。

tmpfs具有爲所有文件設置NUMA內存分配策略掛載選項(如果啓用了CONFIG_NUMA),
可以通過「mount -o remount ...」調整

======================== =========================
mpol=default             採用進程分配策略
                         (請參閱 set_mempolicy(2))
mpol=prefer:Node         傾向從給定的節點分配
mpol=bind:NodeList       只允許從指定的鍊表分配
mpol=interleave          傾向於依次從每個節點分配
mpol=interleave:NodeList 依次從每個節點分配
mpol=local               優先本地節點分配內存
======================== =========================

NodeList格式是以逗號分隔的十進位數字表示大小和範圍，最大和最小範圍是用-
分隔符的十進位數來表示。例如，mpol=bind0-3,5,7,9-15

帶有有效NodeList的內存策略將按指定格式保存，在創建文件時使用。當任務在該
文件系統上創建文件時，會使用到掛載時的內存策略NodeList選項，如果設置的話，
由調用任務的cpuset[請參見Documentation/admin-guide/cgroup-v1/cpusets.rst]
以及下面列出的可選標誌約束。如果NodeLists爲設置爲空集，則文件的內存策略將
恢復爲「默認」策略。

NUMA內存分配策略有可選標誌，可以用於模式結合。在掛載tmpfs時指定這些可選
標誌可以在NodeList之前生效。
Documentation/admin-guide/mm/numa_memory_policy.rst列出所有可用的內存
分配策略模式標誌及其對內存策略。

::

	=static		相當於	MPOL_F_STATIC_NODES
	=relative	相當於	MPOL_F_RELATIVE_NODES

例如，mpol=bind=staticNodeList相當於MPOL_BIND|MPOL_F_STATIC_NODES的分配策略

請注意，如果內核不支持NUMA，那麼使用mpol選項掛載tmpfs將會失敗；nodelist指定不
在線的節點也會失敗。如果您的系統依賴於此，但內核會運行不帶NUMA功能(也許是安全
revocery內核)，或者具有較少的節點在線，建議從自動模式中省略mpol選項掛載選項。
可以在以後通過「mount -o remount,mpol=Policy:NodeList MountPoint」添加到掛載點。

要指定初始根目錄，可以使用如下掛載選項：

====	====================
模式	權限用八進位數字表示
uid	用戶ID
gid	組ID
====	====================

這些選項對remount沒有任何影響。您可以通過chmod(1),chown(1)和chgrp(1)的更改
已經掛載的參數。

tmpfs具有選擇32位還是64位inode的掛載選項：

=======   =============
inode64   使用64位inode
inode32   使用32位inode
=======   =============

在32位內核上，默認是inode32，掛載時指定inode64會被拒絕。
在64位內核上，默認配置是CONFIG_TMPFS_INODE64。inode64避免了單個設備上可能有多個
具有相同inode編號的文件；比如32位應用程式使用glibc如果長期訪問tmpfs，一旦達到33
位inode編號，就有EOVERFLOW失敗的危險，無法打開大於2GiB的文件，並返回EINVAL。

所以'mount -t tmpfs -o size=10G,nr_inodes=10k,mode=700 tmpfs /mytmpfs'將在
/mytmpfs上掛載tmpfs實例，分配只能由root用戶訪問的10GB RAM/SWAP，可以有10240個
inode的實例。


:作者:
   Christoph Rohland <cr@sap.com>, 1.12.01
:更新:
   Hugh Dickins, 4 June 2007
:更新:
   KOSAKI Motohiro, 16 Mar 2010
:更新:
   Chris Down, 13 July 2020

