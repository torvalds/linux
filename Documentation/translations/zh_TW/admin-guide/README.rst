.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/README.rst

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

Linux內核6.x版本 <http://kernel.org/>
=========================================

以下是Linux版本6的發行註記。仔細閱讀它們，
它們會告訴你這些都是什麼，解釋如何安裝內核，以及遇到問題時該如何做。

什麼是Linux？
---------------

  Linux是Unix操作系統的克隆版本，由Linus Torvalds在一個鬆散的網絡黑客
  （Hacker，無貶義）團隊的幫助下從頭開始編寫。它旨在實現兼容POSIX和
  單一UNIX規範。

  它具有在現代成熟的Unix中應當具有的所有功能，包括真正的多任務處理、虛擬內存、
  共享庫、按需加載、共享的寫時拷貝（COW）可執行文件、恰當的內存管理以及包括
  IPv4和IPv6在內的複合網絡棧。

  Linux在GNU通用公共許可證，版本2（GNU GPLv2）下分發，詳見隨附的COPYING文件。

它能在什麼樣的硬件上運行？
-----------------------------

  雖然Linux最初是爲32位的x86 PC機（386或更高版本）開發的，但今天它也能運行在
  （至少）Compaq Alpha AXP、Sun SPARC與UltraSPARC、Motorola 68000、PowerPC、
  PowerPC64、ARM、Hitachi SuperH、Cell、IBM S/390、MIPS、HP PA-RISC、Intel 
  IA-64、DEC VAX、AMD x86-64 Xtensa和ARC架構上。

  Linux很容易移植到大多數通用的32位或64位體系架構，只要它們有一個分頁內存管理
  單元（PMMU）和一個移植的GNU C編譯器（gcc；GNU Compiler Collection，GCC的一
  部分）。Linux也被移植到許多沒有PMMU的體系架構中，儘管功能顯然受到了一定的
  限制。
  Linux也被移植到了其自己上。現在可以將內核作爲用戶空間應用程序運行——這被
  稱爲用戶模式Linux（UML）。

文檔
-----
因特網上和書籍上都有大量的電子文檔，既有Linux專屬文檔，也有與一般UNIX問題相關
的文檔。我建議在任何Linux FTP站點上查找LDP（Linux文檔項目）書籍的文檔子目錄。
本自述文件並不是關於系統的文檔：有更好的可用資源。

 - 因特網上和書籍上都有大量的（電子）文檔，既有Linux專屬文檔，也有與普通
   UNIX問題相關的文檔。我建議在任何有LDP（Linux文檔項目）書籍的Linux FTP
   站點上查找文檔子目錄。本自述文件並不是關於系統的文檔：有更好的可用資源。

 - 文檔/子目錄中有各種自述文件：例如，這些文件通常包含一些特定驅動程序的
   內核安裝說明。請閱讀
   :ref:`Documentation/process/changes.rst <changes>` 文件，它包含了升級內核
   可能會導致的問題的相關信息。

安裝內核源代碼
---------------

 - 如果您要安裝完整的源代碼，請把內核tar檔案包放在您有權限的目錄中（例如您
   的主目錄）並將其解包::

     xz -cd linux-6.x.tar.xz | tar xvf -

   將“X”替換成最新內核的版本號。

   【不要】使用 /usr/src/linux 目錄！這裏有一組庫頭文件使用的內核頭文件
   （通常是不完整的）。它們應該與庫匹配，而不是被內核的變化搞得一團糟。

 - 您還可以通過打補丁在6.x版本之間升級。補丁以xz格式分發。要通過打補丁進行
   安裝，請獲取所有較新的補丁文件，進入內核源代碼（linux-6.x）的目錄並
   執行::

     xz -cd ../patch-6.x.xz | patch -p1

   請【按順序】替換所有大於當前源代碼樹版本的“x”，這樣就可以了。您可能想要
   刪除備份文件（文件名類似xxx~ 或 xxx.orig)，並確保沒有失敗的補丁（文件名
   類似xxx# 或 xxx.rej）。如果有，不是你就是我犯了錯誤。

   與6.x內核的補丁不同，6.x.y內核（也稱爲穩定版內核）的補丁不是增量的，而是
   直接應用於基本的6.x內核。例如，如果您的基本內核是6.0，並且希望應用6.0.3
   補丁，則不應先應用6.0.1和6.0.2的補丁。類似地，如果您運行的是6.0.2內核，
   並且希望跳轉到6.0.3，那麼在應用6.0.3補丁之前，必須首先撤銷6.0.2補丁
   （即patch -R）。更多關於這方面的內容，請閱讀
   :ref:`Documentation/process/applying-patches.rst <applying_patches>` 。

   或者，腳本 patch-kernel 可以用來自動化這個過程。它能確定當前內核版本並
   應用找到的所有補丁::

     linux/scripts/patch-kernel linux

   上面命令中的第一個參數是內核源代碼的位置。補丁是在當前目錄應用的，但是
   可以將另一個目錄指定爲第二個參數。

 - 確保沒有過時的 .o 文件和依賴項::

     cd linux
     make mrproper

   現在您應該已經正確安裝了源代碼。

軟件要求
---------

   編譯和運行6.x內核需要各種軟件包的最新版本。請參考
   :ref:`Documentation/process/changes.rst <changes>`
   來了解最低版本要求以及如何升級軟件包。請注意，使用過舊版本的這些包可能會
   導致很難追蹤的間接錯誤，因此不要以爲在生成或操作過程中出現明顯問題時可以
   只更新包。

爲內核建立目錄
---------------

   編譯內核時，默認情況下所有輸出文件都將與內核源代碼放在一起。使用
   ``make O=output/dir`` 選項可以爲輸出文件（包括 .config）指定備用位置。
   例如::

     kernel source code: /usr/src/linux-6.x
     build directory:    /home/name/build/kernel

   要配置和構建內核，請使用::

     cd /usr/src/linux-6.x
     make O=/home/name/build/kernel menuconfig
     make O=/home/name/build/kernel
     sudo make O=/home/name/build/kernel modules_install install

   請注意：如果使用了 ``O=output/dir`` 選項，那麼它必須用於make的所有調用。

配置內核
---------

   即使只升級一個小版本，也不要跳過此步驟。每個版本中都會添加新的配置選項，
   如果配置文件沒有按預定設置，就會出現奇怪的問題。如果您想以最少的工作量
   將現有配置升級到新版本，請使用 ``make oldconfig`` ，它只會詢問您新配置
   選項的答案。

 - 其他配置命令包括::

     "make config"      純文本界面。

     "make menuconfig"  基於文本的彩色菜單、選項列表和對話框。

     "make nconfig"     增強的基於文本的彩色菜單。

     "make xconfig"     基於Qt的配置工具。

     "make gconfig"     基於GTK的配置工具。

     "make oldconfig"   基於現有的 ./.config 文件選擇所有選項，並詢問
                        新配置選項。

     "make olddefconfig"
                        類似上一個，但不詢問直接將新選項設置爲默認值。

     "make defconfig"   根據體系架構，使用arch/$arch/defconfig或
                        arch/$arch/configs/${PLATFORM}_defconfig中的
                        默認選項值創建./.config文件。

     "make ${PLATFORM}_defconfig"
                        使用arch/$arch/configs/${PLATFORM}_defconfig中
                        的默認選項值創建一個./.config文件。
                        用“make help”來獲取您體系架構中所有可用平臺的列表。

     "make allyesconfig"
                        通過儘可能將選項值設置爲“y”，創建一個
                        ./.config文件。

     "make allmodconfig"
                        通過儘可能將選項值設置爲“m”，創建一個
                        ./.config文件。

     "make allnoconfig" 通過儘可能將選項值設置爲“n”，創建一個
                        ./.config文件。

     "make randconfig"  通過隨機設置選項值來創建./.config文件。

     "make localmodconfig" 基於當前配置和加載的模塊（lsmod）創建配置。禁用
                           已加載的模塊不需要的任何模塊選項。

                           要爲另一臺計算機創建localmodconfig，請將該計算機
                           的lsmod存儲到一個文件中，並將其作爲lsmod參數傳入。

                           此外，通過在參數LMC_KEEP中指定模塊的路徑，可以將
                           模塊保留在某些文件夾或kconfig文件中。

                   target$ lsmod > /tmp/mylsmod
                   target$ scp /tmp/mylsmod host:/tmp

                   host$ make LSMOD=/tmp/mylsmod \
                           LMC_KEEP="drivers/usb:drivers/gpu:fs" \
                           localmodconfig

                           上述方法在交叉編譯時也適用。

     "make localyesconfig" 與localmodconfig類似，只是它會將所有模塊選項轉換
                           爲內置（=y）。你可以同時通過LMC_KEEP保留模塊。

     "make kvm_guest.config"
                        爲kvm客戶機內核支持啓用其他選項。

     "make xen.config"  爲xen dom0客戶機內核支持啓用其他選項。

     "make tinyconfig"  配置儘可能小的內核。

   更多關於使用Linux內核配置工具的信息，見文檔
   Documentation/kbuild/kconfig.rst。

 - ``make config`` 注意事項:

    - 包含不必要的驅動程序會使內核變大，並且在某些情況下會導致問題：
      探測不存在的控制器卡可能會混淆其他控制器。

    - 如果存在協處理器，則編譯了數學仿真的內核仍將使用協處理器：在
      這種情況下，數學仿真永遠不會被使用。內核會稍微大一點，但不管
      是否有數學協處理器，都可以在不同的機器上工作。

    - “kernel hacking”配置細節通常會導致更大或更慢的內核（或兩者
      兼而有之），甚至可以通過配置一些例程來主動嘗試破壞壞代碼以發現
      內核問題，從而降低內核的穩定性（kmalloc()）。因此，您可能應該
      用於研究“開發”、“實驗”或“調試”特性相關問題。

編譯內核
---------

 - 確保您至少有gcc 5.1可用。
   有關更多信息，請參閱 :ref:`Documentation/process/changes.rst <changes>` 。

 - 執行 ``make`` 來創建壓縮內核映像。如果您安裝了lilo以適配內核makefile，
   那麼也可以進行 ``make install`` ，但是您可能需要先檢查特定的lilo設置。

   實際安裝必須以root身份執行，但任何正常構建都不需要。
   無須徒然使用root身份。

 - 如果您將內核的任何部分配置爲模塊，那麼還必須執行 ``make modules_install`` 。

 - 詳細的內核編譯/生成輸出：

   通常，內核構建系統在相當安靜的模式下運行（但不是完全安靜）。但是有時您或
   其他內核開發人員需要看到編譯、鏈接或其他命令的執行過程。爲此，可使用
   “verbose（詳細）”構建模式。
   向 ``make`` 命令傳遞 ``V=1`` 來實現，例如::

     make V=1 all

   如需構建系統也給出內個目標重建的願意，請使用 ``V=2`` 。默認爲 ``V=0`` 。

 - 準備一個備份內核以防出錯。對於開發版本尤其如此，因爲每個新版本都包含
   尚未調試的新代碼。也要確保保留與該內核對應的模塊的備份。如果要安裝
   與工作內核版本號相同的新內核，請在進行 ``make modules_install`` 安裝
   之前備份modules目錄。

   或者，在編譯之前，使用內核配置選項“LOCALVERSION”向常規內核版本附加
   一個唯一的後綴。LOCALVERSION可以在“General Setup”菜單中設置。

 - 爲了引導新內核，您需要將內核映像（例如編譯後的
   .../linux/arch/x86/boot/bzImage）複製到常規可引導內核的位置。

 - 不再支持在沒有LILO等啓動裝載程序幫助的情況下直接從軟盤引導內核。

   如果從硬盤引導Linux，很可能使用LILO，它使用/etc/lilo.conf文件中
   指定的內核映像文件。內核映像文件通常是/vmlinuz、/boot/vmlinuz、
   /bzImage或/boot/bzImage。使用新內核前，請保存舊映像的副本，並複製
   新映像覆蓋舊映像。然後您【必須重新運行LILO】來更新加載映射！否則，
   將無法啓動新的內核映像。

   重新安裝LILO通常需要運行/sbin/LILO。您可能希望編輯/etc/lilo.conf
   文件爲舊內核映像指定一個條目（例如/vmlinux.old)防止新的不能正常
   工作。有關更多信息，請參閱LILO文檔。

   重新安裝LILO之後，您應該就已經準備好了。關閉系統，重新啓動，盡情
   享受吧！

   如果需要更改內核映像中的默認根設備、視頻模式等，請在適當的地方使用
   啓動裝載程序的引導選項。無需重新編譯內核即可更改這些參數。

 - 使用新內核重新啓動並享受它吧。

若遇到問題
-----------

如果您發現了一些可能由於內核缺陷所導致的問題，請參閱：
Documentation/translations/zh_CN/admin-guide/reporting-issues.rst 。

想要理解內核錯誤報告，請參閱：
Documentation/translations/zh_CN/admin-guide/bug-hunting.rst 。

更多用GDB調試內核的信息，請參閱：
Documentation/translations/zh_CN/dev-tools/gdb-kernel-debugging.rst
和 Documentation/dev-tools/kgdb.rst 。

