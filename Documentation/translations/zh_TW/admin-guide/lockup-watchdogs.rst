.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/lockup-watchdogs.rst
:Translator: Hailong Liu <liu.hailong6@zte.com.cn>

.. _tw_lockup-watchdogs:


=================================================
Softlockup與hardlockup檢測機制(又名:nmi_watchdog)
=================================================

Linux中內核實現了一種用以檢測系統發生softlockup和hardlockup的看門狗機制。

Softlockup是一種會引發系統在內核態中一直循環超過20秒（詳見下面“實現”小節）導致
其他任務沒有機會得到運行的BUG。一旦檢測到'softlockup'發生，默認情況下系統會打
印當前堆棧跟蹤信息並進入鎖定狀態。也可配置使其在檢測到'softlockup'後進入panic
狀態；通過sysctl命令設置“kernel.softlockup_panic”、使用內核啓動參數
“softlockup_panic”（詳見Documentation/admin-guide/kernel-parameters.rst）以及使
能內核編譯選項“BOOTPARAM_SOFTLOCKUP_PANIC”都可實現這種配置。

而'hardlockup'是一種會引發系統在內核態一直循環超過10秒鐘（詳見"實現"小節）導致其
他中斷沒有機會運行的缺陷。與'softlockup'情況類似，除了使用sysctl命令設置
'hardlockup_panic'、使能內核選項“BOOTPARAM_HARDLOCKUP_PANIC”以及使用內核參數
"nmi_watchdog"(詳見:”Documentation/admin-guide/kernel-parameters.rst“)外，一旦檢
測到'hardlockup'默認情況下系統打印當前堆棧跟蹤信息，然後進入鎖定狀態。

這個panic選項也可以與panic_timeout結合使用（這個panic_timeout是通過稍具迷惑性的
sysctl命令"kernel.panic"來設置），使系統在panic指定時間後自動重啓。

實現
====

Softlockup和hardlockup分別建立在hrtimer(高精度定時器)和perf兩個子系統上而實現。
這也就意味着理論上任何架構只要實現了這兩個子系統就支持這兩種檢測機制。

Hrtimer用於週期性產生中斷並喚醒watchdog線程；NMI perf事件則以”watchdog_thresh“
(編譯時默認初始化爲10秒，也可通過”watchdog_thresh“這個sysctl接口來進行配置修改)
爲間隔週期產生以檢測 hardlockups。如果一個CPU在這個時間段內沒有檢測到hrtimer中
斷髮生，'hardlockup 檢測器'(即NMI perf事件處理函數)將會視系統配置而選擇產生內核
警告或者直接panic。

而watchdog線程本質上是一個高優先級內核線程，每調度一次就對時間戳進行一次更新。
如果時間戳在2*watchdog_thresh(這個是softlockup的觸發門限)這段時間都未更新,那麼
"softlocup 檢測器"(內部hrtimer定時器回調函數)會將相關的調試信息打印到系統日誌中，
然後如果系統配置了進入panic流程則進入panic，否則內核繼續執行。

Hrtimer定時器的週期是2*watchdog_thresh/5，也就是說在hardlockup被觸發前hrtimer有
2~3次機會產生時鐘中斷。

如上所述,內核相當於爲系統管理員提供了一個可調節hrtimer定時器和perf事件週期長度
的調節旋鈕。如何通過這個旋鈕爲特定使用場景配置一個合理的週期值要對lockups檢測的
響應速度和lockups檢測開銷這二者之間進行權衡。

默認情況下所有在線cpu上都會運行一個watchdog線程。不過在內核配置了”NO_HZ_FULL“的
情況下watchdog線程默認只會運行在管家(housekeeping)cpu上，而”nohz_full“啓動參數指
定的cpu上則不會有watchdog線程運行。試想，如果我們允許watchdog線程在”nohz_full“指
定的cpu上運行，這些cpu上必須得運行時鐘定時器來激發watchdog線程調度；這樣一來就會
使”nohz_full“保護用戶程序免受內核干擾的功能失效。當然，副作用就是”nohz_full“指定
的cpu即使在內核產生了lockup問題我們也無法檢測到。不過，至少我們可以允許watchdog
線程在管家(non-tickless)核上繼續運行以便我們能繼續正常的監測這些cpus上的lockups
事件。

不論哪種情況都可以通過sysctl命令kernel.watchdog_cpumask來對沒有運行watchdog線程
的cpu集合進行調節。對於nohz_full而言,如果nohz_full cpu上有異常掛住的情況，通過
這種方式打開這些cpu上的watchdog進行調試可能會有所作用。

