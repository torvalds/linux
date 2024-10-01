.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/dev-tools/gcov.rst
:Translator: 趙軍奎 Bernard Zhao <bernard@vivo.com>

在Linux內核裏使用gcov做代碼覆蓋率檢查
=====================================

gcov分析核心支持在Linux內核中啓用GCC的覆蓋率測試工具 gcov_ ，Linux內核
運行時的代碼覆蓋率數據會以gcov兼容的格式導出到“gcov”debugfs目錄中，可
以通過gcov的 ``-o`` 選項（如下示例）獲得指定文件的代碼運行覆蓋率統計數據
（需要跳轉到內核編譯路徑下並且要有root權限）::

    # cd /tmp/linux-out
    # gcov -o /sys/kernel/debug/gcov/tmp/linux-out/kernel spinlock.c

這將在當前目錄中創建帶有執行計數註釋的源代碼文件。
在獲得這些統計文件後，可以使用圖形化的gcov前端工具（比如 lcov_ ），來實現
自動化處理Linux內核的覆蓋率運行數據，同時生成易於閱讀的HTML格式文件。

可能的用途:

* 調試（用來判斷每一行的代碼是否已經運行過）
* 測試改進（如何修改測試代碼，儘可能地覆蓋到沒有運行過的代碼）
* 內核最小化配置（對於某一個選項配置，如果關聯的代碼從來沒有運行過，
  是否還需要這個配置）

.. _gcov: https://gcc.gnu.org/onlinedocs/gcc/Gcov.html
.. _lcov: http://ltp.sourceforge.net/coverage/lcov.php


準備
----

內核打開如下配置::

        CONFIG_DEBUG_FS=y
        CONFIG_GCOV_KERNEL=y

獲取整個內核的覆蓋率數據，還需要打開::

        CONFIG_GCOV_PROFILE_ALL=y

需要注意的是，整個內核開啓覆蓋率統計會造成內核鏡像文件尺寸的增大，
同時內核運行也會變慢一些。
另外，並不是所有的架構都支持整個內核開啓覆蓋率統計。

代碼運行覆蓋率數據只在debugfs掛載完成後纔可以訪問::

        mount -t debugfs none /sys/kernel/debug


定製化
------

如果要單獨針對某一個路徑或者文件進行代碼覆蓋率統計，可以在內核相應路
徑的Makefile中增加如下的配置:

- 單獨統計單個文件（例如main.o）::

    GCOV_PROFILE_main.o := y

- 單獨統計某一個路徑::

    GCOV_PROFILE := y

如果要在整個內核的覆蓋率統計（開啓CONFIG_GCOV_PROFILE_ALL）中單獨排除
某一個文件或者路徑，可以使用如下的方法::

    GCOV_PROFILE_main.o := n

和::

    GCOV_PROFILE := n

此機制僅支持鏈接到內核鏡像或編譯爲內核模塊的文件。


相關文件
--------

gcov功能需要在debugfs中創建如下文件:

``/sys/kernel/debug/gcov``
    gcov相關功能的根路徑

``/sys/kernel/debug/gcov/reset``
    全局復位文件:向該文件寫入數據後會將所有的gcov統計數據清0

``/sys/kernel/debug/gcov/path/to/compile/dir/file.gcda``
    gcov工具可以識別的覆蓋率統計數據文件，向該文件寫入數據後
	  會將本文件的gcov統計數據清0

``/sys/kernel/debug/gcov/path/to/compile/dir/file.gcno``
    gcov工具需要的軟連接文件（指向編譯時生成的信息統計文件），這個文件是
    在gcc編譯時如果配置了選項 ``-ftest-coverage`` 時生成的。


針對模塊的統計
--------------

內核中的模塊會動態的加載和卸載，模塊卸載時對應的數據會被清除掉。
gcov提供了一種機制，通過保留相關數據的副本來收集這部分卸載模塊的覆蓋率數據。
模塊卸載後這些備份數據在debugfs中會繼續存在。
一旦這個模塊重新加載，模塊關聯的運行統計會被初始化成debugfs中備份的數據。

可以通過對內核參數gcov_persist的修改來停用gcov對模塊的備份機制::

        gcov_persist = 0

在運行時，用戶還可以通過寫入模塊的數據文件或者寫入gcov復位文件來丟棄已卸
載模塊的數據。


編譯機和測試機分離
------------------

gcov的內核分析插樁支持內核的編譯和運行是在同一臺機器上，也可以編譯和運
行是在不同的機器上。
如果內核編譯和運行是不同的機器，那麼需要額外的準備工作，這取決於gcov工具
是在哪裏使用的:

.. _gcov-test_zh:

a) 若gcov運行在測試機上

    測試機上面gcov工具的版本必須要跟內核編譯機器使用的gcc版本相兼容，
    同時下面的文件要從編譯機拷貝到測試機上:

    從源代碼中:
      - 所有的C文件和頭文件

    從編譯目錄中:
      - 所有的C文件和頭文件
      - 所有的.gcda文件和.gcno文件
      - 所有目錄的鏈接

    特別需要注意，測試機器上面的目錄結構跟編譯機器上面的目錄機構必須
    完全一致。
    如果文件是軟鏈接，需要替換成真正的目錄文件（這是由make的當前工作
    目錄變量CURDIR引起的）。

.. _gcov-build_zh:

b) 若gcov運行在編譯機上

    測試用例運行結束後，如下的文件需要從測試機中拷貝到編譯機上:

    從sysfs中的gcov目錄中:
      - 所有的.gcda文件
      - 所有的.gcno文件軟鏈接

    這些文件可以拷貝到編譯機的任意目錄下，gcov使用-o選項指定拷貝的
    目錄。

    比如一個是示例的目錄結構如下::

      /tmp/linux:    內核源碼目錄
      /tmp/out:      內核編譯文件路徑（make O=指定）
      /tmp/coverage: 從測試機器上面拷貝的數據文件路徑

      [user@build] cd /tmp/out
      [user@build] gcov -o /tmp/coverage/tmp/out/init main.c


關於編譯器的注意事項
--------------------

GCC和LLVM gcov工具不一定兼容。
如果編譯器是GCC，使用 gcov_ 來處理.gcno和.gcda文件，如果是Clang編譯器，
則使用 llvm-cov_ 。

.. _gcov: https://gcc.gnu.org/onlinedocs/gcc/Gcov.html
.. _llvm-cov: https://llvm.org/docs/CommandGuide/llvm-cov.html

GCC和Clang gcov之間的版本差異由Kconfig處理的。
kconfig會根據編譯工具鏈的檢查自動選擇合適的gcov格式。

問題定位
--------

可能出現的問題1
    編譯到鏈接階段報錯終止

問題原因
    分析標誌指定在了源文件但是沒有鏈接到主內核，或者客製化了鏈接程序

解決方法
    通過在相應的Makefile中使用 ``GCOV_PROFILE := n``
    或者 ``GCOV_PROFILE_basename.o := n`` 來將鏈接報錯的文件排除掉

可能出現的問題2
    從sysfs複製的文件顯示爲空或不完整

問題原因
    由於seq_file的工作方式，某些工具（例如cp或tar）可能無法正確地從
    sysfs複製文件。

解決方法
    使用 ``cat`` 讀取 ``.gcda`` 文件，使用 ``cp -d`` 複製鏈接，或者使用附錄B
    中所示的機制。


附錄A：collect_on_build.sh
--------------------------

用於在編譯機上收集覆蓋率元文件的示例腳本
（見 :ref:`編譯機和測試機分離 a. <gcov-test_zh>` ）

.. code-block:: sh

    #!/bin/bash

    KSRC=$1
    KOBJ=$2
    DEST=$3

    if [ -z "$KSRC" ] || [ -z "$KOBJ" ] || [ -z "$DEST" ]; then
      echo "Usage: $0 <ksrc directory> <kobj directory> <output.tar.gz>" >&2
      exit 1
    fi

    KSRC=$(cd $KSRC; printf "all:\n\t@echo \${CURDIR}\n" | make -f -)
    KOBJ=$(cd $KOBJ; printf "all:\n\t@echo \${CURDIR}\n" | make -f -)

    find $KSRC $KOBJ \( -name '*.gcno' -o -name '*.[ch]' -o -type l \) -a \
                     -perm /u+r,g+r | tar cfz $DEST -P -T -

    if [ $? -eq 0 ] ; then
      echo "$DEST successfully created, copy to test system and unpack with:"
      echo "  tar xfz $DEST -P"
    else
      echo "Could not create file $DEST"
    fi


附錄B：collect_on_test.sh
-------------------------

用於在測試機上收集覆蓋率數據文件的示例腳本
（見 :ref:`編譯機和測試機分離 b. <gcov-build_zh>` ）

.. code-block:: sh

    #!/bin/bash -e

    DEST=$1
    GCDA=/sys/kernel/debug/gcov

    if [ -z "$DEST" ] ; then
      echo "Usage: $0 <output.tar.gz>" >&2
      exit 1
    fi

    TEMPDIR=$(mktemp -d)
    echo Collecting data..
    find $GCDA -type d -exec mkdir -p $TEMPDIR/\{\} \;
    find $GCDA -name '*.gcda' -exec sh -c 'cat < $0 > '$TEMPDIR'/$0' {} \;
    find $GCDA -name '*.gcno' -exec sh -c 'cp -d $0 '$TEMPDIR'/$0' {} \;
    tar czf $DEST -C $TEMPDIR sys
    rm -rf $TEMPDIR

    echo "$DEST successfully created, copy to build system and unpack with:"
    echo "  tar xfz $DEST"

