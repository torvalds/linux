Chinese translated version of Documentation/dev-tools/sparse.rst

If you have any comment or update to the content, please contact the
original document maintainer directly.  However, if you have a problem
communicating in English you can also ask the Chinese maintainer for
help.  Contact the Chinese maintainer if this translation is outdated
or if there is a problem with the translation.

Traditional Chinese maintainer: Hu Haowen <src.res.211@gmail.com>
---------------------------------------------------------------------
Documentation/dev-tools/sparse.rst 的繁體中文翻譯

如果想評論或更新本文的內容，請直接聯繫原文檔的維護者。如果你使用英文
交流有困難的話，也可以向繁體中文版維護者求助。如果本翻譯更新不及時或
者翻譯存在問題，請聯繫繁體中文版維護者。

繁體中文版維護者： 胡皓文 Hu Haowen <src.res.211@gmail.com>
繁體中文版翻譯者： 胡皓文 Hu Haowen <src.res.211@gmail.com>

以下爲正文
---------------------------------------------------------------------

Copyright 2004 Linus Torvalds
Copyright 2004 Pavel Machek <pavel@ucw.cz>
Copyright 2006 Bob Copeland <me@bobcopeland.com>

使用 sparse 工具做類型檢查
~~~~~~~~~~~~~~~~~~~~~~~~~~

"__bitwise" 是一種類型屬性，所以你應該這樣使用它::

        typedef int __bitwise pm_request_t;

        enum pm_request {
                PM_SUSPEND = (__force pm_request_t) 1,
                PM_RESUME = (__force pm_request_t) 2
        };

這樣會使 PM_SUSPEND 和 PM_RESUME 成爲位方式(bitwise)整數（使用"__force"
是因爲 sparse 會抱怨改變位方式的類型轉換，但是這裡我們確實需要強制進行轉
換）。而且因爲所有枚舉值都使用了相同的類型，這裡的"enum pm_request"也將
會使用那個類型做爲底層實現。

而且使用 gcc 編譯的時候，所有的 __bitwise/__force 都會消失，最後在 gcc
看來它們只不過是普通的整數。

坦白來說，你並不需要使用枚舉類型。上面那些實際都可以濃縮成一個特殊的"int
__bitwise"類型。

所以更簡單的辦法只要這樣做::

	typedef int __bitwise pm_request_t;

	#define PM_SUSPEND ((__force pm_request_t) 1)
	#define PM_RESUME ((__force pm_request_t) 2)

現在你就有了嚴格的類型檢查所需要的所有基礎架構。

一個小提醒：常數整數"0"是特殊的。你可以直接把常數零當作位方式整數使用而
不用擔心 sparse 會抱怨。這是因爲"bitwise"（恰如其名）是用來確保不同位方
式類型不會被弄混（小尾模式，大尾模式，cpu尾模式，或者其他），對他們來說
常數"0"確實是特殊的。

獲取 sparse 工具
~~~~~~~~~~~~~~~~

你可以從 Sparse 的主頁獲取最新的發布版本：

	https://www.kernel.org/pub/software/devel/sparse/dist/

或者，你也可以使用 git 克隆最新的 sparse 開發版本：

        git://git.kernel.org/pub/scm/devel/sparse/sparse.git

一旦你下載了源碼，只要以普通用戶身份運行：

	make
	make install

它將會被自動安裝到你的 ~/bin 目錄下。

使用 sparse 工具
~~~~~~~~~~~~~~~~

用"make C=1"命令來編譯內核，會對所有重新編譯的 C 文件使用 sparse 工具。
或者使用"make C=2"命令，無論文件是否被重新編譯都會對其使用 sparse 工具。
如果你已經編譯了內核，用後一種方式可以很快地檢查整個源碼樹。

make 的可選變量 CHECKFLAGS 可以用來向 sparse 工具傳遞參數。編譯系統會自
動向 sparse 工具傳遞 -Wbitwise 參數。

