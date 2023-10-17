.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/coding-style.rst <codingstyle>`

.. _tw_codingstyle:

譯者::

  中文版維護者： 張樂 Zhang Le <r0bertz@gentoo.org>
  中文版翻譯者： 張樂 Zhang Le <r0bertz@gentoo.org>
  中文版校譯者： 王聰 Wang Cong <xiyou.wangcong@gmail.com>
                 wheelz <kernel.zeng@gmail.com>
                 管旭東 Xudong Guan <xudong.guan@gmail.com>
                 Li Zefan <lizf@cn.fujitsu.com>
                 Wang Chen <wangchen@cn.fujitsu.com>
                 Hu Haowen <src.res.211@gmail.com>

Linux 內核代碼風格
=========================

這是一個簡短的文檔，描述了 linux 內核的首選代碼風格。代碼風格是因人而異的，
而且我不願意把自己的觀點強加給任何人，但這就像我去做任何事情都必須遵循的原則
那樣，我也希望在絕大多數事上保持這種的態度。請 (在寫代碼時) 至少考慮一下這裡
的代碼風格。

首先，我建議你列印一份 GNU 代碼規範，然後不要讀。燒了它，這是一個具有重大象徵
性意義的動作。

不管怎樣，現在我們開始：


1) 縮進
--------------

制表符是 8 個字符，所以縮進也是 8 個字符。有些異端運動試圖將縮進變爲 4 (甚至
2！) 字符深，這幾乎相當於嘗試將圓周率的值定義爲 3。

理由：縮進的全部意義就在於清楚的定義一個控制塊起止於何處。尤其是當你盯著你的
屏幕連續看了 20 小時之後，你將會發現大一點的縮進會使你更容易分辨縮進。

現在，有些人會抱怨 8 個字符的縮進會使代碼向右邊移動的太遠，在 80 個字符的終端
屏幕上就很難讀這樣的代碼。這個問題的答案是，如果你需要 3 級以上的縮進，不管用
何種方式你的代碼已經有問題了，應該修正你的程序。

簡而言之，8 個字符的縮進可以讓代碼更容易閱讀，還有一個好處是當你的函數嵌套太
深的時候可以給你警告。留心這個警告。

在 switch 語句中消除多級縮進的首選的方式是讓 ``switch`` 和從屬於它的 ``case``
標籤對齊於同一列，而不要 ``兩次縮進`` ``case`` 標籤。比如：

.. code-block:: c

	switch (suffix) {
	case 'G':
	case 'g':
		mem <<= 30;
		break;
	case 'M':
	case 'm':
		mem <<= 20;
		break;
	case 'K':
	case 'k':
		mem <<= 10;
		fallthrough;
	default:
		break;
	}

不要把多個語句放在一行里，除非你有什麼東西要隱藏：

.. code-block:: c

	if (condition) do_this;
	  do_something_everytime;

也不要在一行里放多個賦值語句。內核代碼風格超級簡單。就是避免可能導致別人誤讀
的表達式。

除了注釋、文檔和 Kconfig 之外，不要使用空格來縮進，前面的例子是例外，是有意爲
之。

選用一個好的編輯器，不要在行尾留空格。


2) 把長的行和字符串打散
------------------------------

代碼風格的意義就在於使用平常使用的工具來維持代碼的可讀性和可維護性。

每一行的長度的限制是 80 列，我們強烈建議您遵守這個慣例。

長於 80 列的語句要打散成有意義的片段。除非超過 80 列能顯著增加可讀性，並且不
會隱藏信息。子片段要明顯短於母片段，並明顯靠右。這同樣適用於有著很長參數列表
的函數頭。然而，絕對不要打散對用戶可見的字符串，例如 printk 信息，因爲這樣就
很難對它們 grep。


3) 大括號和空格的放置
------------------------------

C 語言風格中另外一個常見問題是大括號的放置。和縮進大小不同，選擇或棄用某种放
置策略並沒有多少技術上的原因，不過首選的方式，就像 Kernighan 和 Ritchie 展示
給我們的，是把起始大括號放在行尾，而把結束大括號放在行首，所以：

.. code-block:: c

	if (x is true) {
		we do y
	}

這適用於所有的非函數語句塊 (if, switch, for, while, do)。比如：

.. code-block:: c

	switch (action) {
	case KOBJ_ADD:
		return "add";
	case KOBJ_REMOVE:
		return "remove";
	case KOBJ_CHANGE:
		return "change";
	default:
		return NULL;
	}

不過，有一個例外，那就是函數：函數的起始大括號放置於下一行的開頭，所以：

.. code-block:: c

	int function(int x)
	{
		body of function
	}

全世界的異端可能會抱怨這個不一致性是... 呃... 不一致的，不過所有思維健全的人
都知道 (a) K&R 是 **正確的** 並且 (b) K&R 是正確的。此外，不管怎樣函數都是特
殊的 (C 函數是不能嵌套的)。

注意結束大括號獨自占據一行，除非它後面跟著同一個語句的剩餘部分，也就是 do 語
句中的 "while" 或者 if 語句中的 "else"，像這樣：

.. code-block:: c

	do {
		body of do-loop
	} while (condition);

和

.. code-block:: c

	if (x == y) {
		..
	} else if (x > y) {
		...
	} else {
		....
	}

理由：K&R。

也請注意這種大括號的放置方式也能使空 (或者差不多空的) 行的數量最小化，同時不
失可讀性。因此，由於你的屏幕上的新行是不可再生資源 (想想 25 行的終端屏幕)，你
將會有更多的空行來放置注釋。

當只有一個單獨的語句的時候，不用加不必要的大括號。

.. code-block:: c

	if (condition)
		action();

和

.. code-block:: c

	if (condition)
		do_this();
	else
		do_that();

這並不適用於只有一個條件分支是單語句的情況；這時所有分支都要使用大括號：

.. code-block:: c

	if (condition) {
		do_this();
		do_that();
	} else {
		otherwise();
	}

3.1) 空格
********************

Linux 內核的空格使用方式 (主要) 取決於它是用於函數還是關鍵字。(大多數) 關鍵字
後要加一個空格。值得注意的例外是 sizeof, typeof, alignof 和 __attribute__，這
些關鍵字某些程度上看起來更像函數 (它們在 Linux 里也常常伴隨小括號而使用，儘管
在 C 里這樣的小括號不是必需的，就像 ``struct fileinfo info;`` 聲明過後的
``sizeof info``)。

所以在這些關鍵字之後放一個空格::

	if, switch, case, for, do, while

但是不要在 sizeof, typeof, alignof 或者 __attribute__ 這些關鍵字之後放空格。
例如，

.. code-block:: c

	s = sizeof(struct file);

不要在小括號里的表達式兩側加空格。這是一個 **反例** ：

.. code-block:: c

	s = sizeof( struct file );

當聲明指針類型或者返回指針類型的函數時， ``*`` 的首選使用方式是使之靠近變量名
或者函數名，而不是靠近類型名。例子：

.. code-block:: c

	char *linux_banner;
	unsigned long long memparse(char *ptr, char **retptr);
	char *match_strdup(substring_t *s);

在大多數二元和三元操作符兩側使用一個空格，例如下面所有這些操作符::

	=  +  -  <  >  *  /  %  |  &  ^  <=  >=  ==  !=  ?  :

但是一元操作符後不要加空格::

	&  *  +  -  ~  !  sizeof  typeof  alignof  __attribute__  defined

後綴自加和自減一元操作符前不加空格::

	++  --

前綴自加和自減一元操作符後不加空格::

	++  --

``.`` 和 ``->`` 結構體成員操作符前後不加空格。

不要在行尾留空白。有些可以自動縮進的編輯器會在新行的行首加入適量的空白，然後
你就可以直接在那一行輸入代碼。不過假如你最後沒有在那一行輸入代碼，有些編輯器
就不會移除已經加入的空白，就像你故意留下一個只有空白的行。包含行尾空白的行就
這樣產生了。

當 git 發現補丁包含了行尾空白的時候會警告你，並且可以應你的要求去掉行尾空白；
不過如果你是正在打一系列補丁，這樣做會導致後面的補丁失敗，因爲你改變了補丁的
上下文。


4) 命名
------------------------------

C 是一個簡樸的語言，你的命名也應該這樣。和 Modula-2 和 Pascal 程式設計師不同，
C 程式設計師不使用類似 ThisVariableIsATemporaryCounter 這樣華麗的名字。C 程式設計師會
稱那個變量爲 ``tmp`` ，這樣寫起來會更容易，而且至少不會令其難於理解。

不過，雖然混用大小寫的名字是不提倡使用的，但是全局變量還是需要一個具描述性的
名字。稱一個全局函數爲 ``foo`` 是一個難以饒恕的錯誤。

全局變量 (只有當你 **真正** 需要它們的時候再用它) 需要有一個具描述性的名字，就
像全局函數。如果你有一個可以計算活動用戶數量的函數，你應該叫它
``count_active_users()`` 或者類似的名字，你不應該叫它 ``cntuser()`` 。

在函數名中包含函數類型 (所謂的匈牙利命名法) 是腦子出了問題——編譯器知道那些類
型而且能夠檢查那些類型，這樣做只能把程式設計師弄糊塗了。難怪微軟總是製造出有問題
的程序。

本地變量名應該簡短，而且能夠表達相關的含義。如果你有一些隨機的整數型的循環計
數器，它應該被稱爲 ``i`` 。叫它 ``loop_counter`` 並無益處，如果它沒有被誤解的
可能的話。類似的， ``tmp`` 可以用來稱呼任意類型的臨時變量。

如果你怕混淆了你的本地變量名，你就遇到另一個問題了，叫做函數增長荷爾蒙失衡綜
合症。請看第六章 (函數)。


5) Typedef
-----------

不要使用類似 ``vps_t`` 之類的東西。

對結構體和指針使用 typedef 是一個 **錯誤** 。當你在代碼里看到：

.. code-block:: c

	vps_t a;

這代表什麼意思呢？

相反，如果是這樣

.. code-block:: c

	struct virtual_container *a;

你就知道 ``a`` 是什麼了。

很多人認爲 typedef ``能提高可讀性`` 。實際不是這樣的。它們只在下列情況下有用：

 (a) 完全不透明的對象 (這種情況下要主動使用 typedef 來 **隱藏** 這個對象實際上
     是什麼)。

     例如： ``pte_t`` 等不透明對象，你只能用合適的訪問函數來訪問它們。

     .. note::

       不透明性和 "訪問函數" 本身是不好的。我們使用 pte_t 等類型的原因在於真
       的是完全沒有任何共用的可訪問信息。

 (b) 清楚的整數類型，如此，這層抽象就可以 **幫助** 消除到底是 ``int`` 還是
     ``long`` 的混淆。

     u8/u16/u32 是完全沒有問題的 typedef，不過它們更符合類別 (d) 而不是這裡。

     .. note::

       要這樣做，必須事出有因。如果某個變量是 ``unsigned long`` ，那麼沒有必要

	typedef unsigned long myflags_t;

     不過如果有一個明確的原因，比如它在某種情況下可能會是一個 ``unsigned int``
     而在其他情況下可能爲 ``unsigned long`` ，那麼就不要猶豫，請務必使用
     typedef。

 (c) 當你使用 sparse 按字面的創建一個 **新** 類型來做類型檢查的時候。

 (d) 和標準 C99 類型相同的類型，在某些例外的情況下。

     雖然讓眼睛和腦筋來適應新的標準類型比如 ``uint32_t`` 不需要花很多時間，可
     是有些人仍然拒絕使用它們。

     因此，Linux 特有的等同於標準類型的 ``u8/u16/u32/u64`` 類型和它們的有符號
     類型是被允許的——儘管在你自己的新代碼中，它們不是強制要求要使用的。

     當編輯已經使用了某個類型集的已有代碼時，你應該遵循那些代碼中已經做出的選
     擇。

 (e) 可以在用戶空間安全使用的類型。

     在某些用戶空間可見的結構體裡，我們不能要求 C99 類型而且不能用上面提到的
     ``u32`` 類型。因此，我們在與用戶空間共享的所有結構體中使用 __u32 和類似
     的類型。

可能還有其他的情況，不過基本的規則是 **永遠不要** 使用 typedef，除非你可以明
確的應用上述某個規則中的一個。

總的來說，如果一個指針或者一個結構體裡的元素可以合理的被直接訪問到，那麼它們
就不應該是一個 typedef。


6) 函數
------------------------------

函數應該簡短而漂亮，並且只完成一件事情。函數應該可以一屏或者兩屏顯示完 (我們
都知道 ISO/ANSI 屏幕大小是 80x24)，只做一件事情，而且把它做好。

一個函數的最大長度是和該函數的複雜度和縮進級數成反比的。所以，如果你有一個理
論上很簡單的只有一個很長 (但是簡單) 的 case 語句的函數，而且你需要在每個 case
里做很多很小的事情，這樣的函數儘管很長，但也是可以的。

不過，如果你有一個複雜的函數，而且你懷疑一個天分不是很高的高中一年級學生可能
甚至搞不清楚這個函數的目的，你應該嚴格遵守前面提到的長度限制。使用輔助函數，
並爲之取個具描述性的名字 (如果你覺得它們的性能很重要的話，可以讓編譯器內聯它
們，這樣的效果往往會比你寫一個複雜函數的效果要好。)

函數的另外一個衡量標準是本地變量的數量。此數量不應超過 5－10 個，否則你的函數
就有問題了。重新考慮一下你的函數，把它分拆成更小的函數。人的大腦一般可以輕鬆
的同時跟蹤 7 個不同的事物，如果再增多的話，就會糊塗了。即便你聰穎過人，你也可
能會記不清你 2 個星期前做過的事情。

在源文件里，使用空行隔開不同的函數。如果該函數需要被導出，它的 **EXPORT** 宏
應該緊貼在它的結束大括號之下。比如：

.. code-block:: c

	int system_is_up(void)
	{
		return system_state == SYSTEM_RUNNING;
	}
	EXPORT_SYMBOL(system_is_up);

在函數原型中，包含函數名和它們的數據類型。雖然 C 語言裡沒有這樣的要求，在
Linux 里這是提倡的做法，因爲這樣可以很簡單的給讀者提供更多的有價值的信息。


7) 集中的函數退出途徑
------------------------------

雖然被某些人聲稱已經過時，但是 goto 語句的等價物還是經常被編譯器所使用，具體
形式是無條件跳轉指令。

當一個函數從多個位置退出，並且需要做一些類似清理的常見操作時，goto 語句就很方
便了。如果並不需要清理操作，那麼直接 return 即可。

選擇一個能夠說明 goto 行爲或它爲何存在的標籤名。如果 goto 要釋放 ``buffer``,
一個不錯的名字可以是 ``out_free_buffer:`` 。別去使用像 ``err1:`` 和 ``err2:``
這樣的GW_BASIC 名稱，因爲一旦你添加或刪除了 (函數的) 退出路徑，你就必須對它們
重新編號，這樣會難以去檢驗正確性。

使用 goto 的理由是：

- 無條件語句容易理解和跟蹤
- 嵌套程度減小
- 可以避免由於修改時忘記更新個別的退出點而導致錯誤
- 讓編譯器省去刪除冗餘代碼的工作 ;)

.. code-block:: c

	int fun(int a)
	{
		int result = 0;
		char *buffer;

		buffer = kmalloc(SIZE, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		if (condition1) {
			while (loop1) {
				...
			}
			result = 1;
			goto out_free_buffer;
		}
		...
	out_free_buffer:
		kfree(buffer);
		return result;
	}

一個需要注意的常見錯誤是 ``一個 err 錯誤`` ，就像這樣：

.. code-block:: c

	err:
		kfree(foo->bar);
		kfree(foo);
		return ret;

這段代碼的錯誤是，在某些退出路徑上 ``foo`` 是 NULL。通常情況下，通過把它分離
成兩個錯誤標籤 ``err_free_bar:`` 和 ``err_free_foo:`` 來修復這個錯誤：

.. code-block:: c

	 err_free_bar:
		kfree(foo->bar);
	 err_free_foo:
		kfree(foo);
		return ret;

理想情況下，你應該模擬錯誤來測試所有退出路徑。


8) 注釋
------------------------------

注釋是好的，不過有過度注釋的危險。永遠不要在注釋里解釋你的代碼是如何運作的：
更好的做法是讓別人一看你的代碼就可以明白，解釋寫的很差的代碼是浪費時間。

一般的，你想要你的注釋告訴別人你的代碼做了什麼，而不是怎麼做的。也請你不要把
注釋放在一個函數體內部：如果函數複雜到你需要獨立的注釋其中的一部分，你很可能
需要回到第六章看一看。你可以做一些小注釋來註明或警告某些很聰明 (或者槽糕) 的
做法，但不要加太多。你應該做的，是把注釋放在函數的頭部，告訴人們它做了什麼，
也可以加上它做這些事情的原因。

當注釋內核 API 函數時，請使用 kernel-doc 格式。請看
Documentation/doc-guide/ 和 scripts/kernel-doc 以獲得詳細信息。

長 (多行) 注釋的首選風格是：

.. code-block:: c

	/*
	 * This is the preferred style for multi-line
	 * comments in the Linux kernel source code.
	 * Please use it consistently.
	 *
	 * Description:  A column of asterisks on the left side,
	 * with beginning and ending almost-blank lines.
	 */

對於在 net/ 和 drivers/net/ 的文件，首選的長 (多行) 注釋風格有些不同。

.. code-block:: c

	/* The preferred comment style for files in net/ and drivers/net
	 * looks like this.
	 *
	 * It is nearly the same as the generally preferred comment style,
	 * but there is no initial almost-blank line.
	 */

注釋數據也是很重要的，不管是基本類型還是衍生類型。爲了方便實現這一點，每一行
應只聲明一個數據 (不要使用逗號來一次聲明多個數據)。這樣你就有空間來爲每個數據
寫一段小注釋來解釋它們的用途了。


9) 你已經把事情弄糟了
------------------------------

這沒什麼，我們都是這樣。可能你的使用了很長時間 Unix 的朋友已經告訴你
``GNU emacs`` 能自動幫你格式化 C 原始碼，而且你也注意到了，確實是這樣，不過它
所使用的默認值和我們想要的相去甚遠 (實際上，甚至比隨機打的還要差——無數個猴子
在 GNU emacs 里打字永遠不會創造出一個好程序) (譯註：Infinite Monkey Theorem)

所以你要麼放棄 GNU emacs，要麼改變它讓它使用更合理的設定。要採用後一個方案，
你可以把下面這段粘貼到你的 .emacs 文件里。

.. code-block:: none

  (defun c-lineup-arglist-tabs-only (ignored)
    "Line up argument lists by tabs, not spaces"
    (let* ((anchor (c-langelem-pos c-syntactic-element))
           (column (c-langelem-2nd-pos c-syntactic-element))
           (offset (- (1+ column) anchor))
           (steps (floor offset c-basic-offset)))
      (* (max steps 1)
         c-basic-offset)))

  (dir-locals-set-class-variables
   'linux-kernel
   '((c-mode . (
          (c-basic-offset . 8)
          (c-label-minimum-indentation . 0)
          (c-offsets-alist . (
                  (arglist-close         . c-lineup-arglist-tabs-only)
                  (arglist-cont-nonempty .
		      (c-lineup-gcc-asm-reg c-lineup-arglist-tabs-only))
                  (arglist-intro         . +)
                  (brace-list-intro      . +)
                  (c                     . c-lineup-C-comments)
                  (case-label            . 0)
                  (comment-intro         . c-lineup-comment)
                  (cpp-define-intro      . +)
                  (cpp-macro             . -1000)
                  (cpp-macro-cont        . +)
                  (defun-block-intro     . +)
                  (else-clause           . 0)
                  (func-decl-cont        . +)
                  (inclass               . +)
                  (inher-cont            . c-lineup-multi-inher)
                  (knr-argdecl-intro     . 0)
                  (label                 . -1000)
                  (statement             . 0)
                  (statement-block-intro . +)
                  (statement-case-intro  . +)
                  (statement-cont        . +)
                  (substatement          . +)
                  ))
          (indent-tabs-mode . t)
          (show-trailing-whitespace . t)
          ))))

  (dir-locals-set-directory-class
   (expand-file-name "~/src/linux-trees")
   'linux-kernel)

這會讓 emacs 在 ``~/src/linux-trees`` 下的 C 源文件獲得更好的內核代碼風格。

不過就算你嘗試讓 emacs 正確的格式化代碼失敗了，也並不意味著你失去了一切：還可
以用 ``indent`` 。

不過，GNU indent 也有和 GNU emacs 一樣有問題的設定，所以你需要給它一些命令選
項。不過，這還不算太糟糕，因爲就算是 GNU indent 的作者也認同 K&R 的權威性
(GNU 的人並不是壞人，他們只是在這個問題上被嚴重的誤導了)，所以你只要給 indent
指定選項 ``-kr -i8`` (代表 ``K&R，8 字符縮進``)，或使用 ``scripts/Lindent``
這樣就可以以最時髦的方式縮進原始碼。

``indent`` 有很多選項，特別是重新格式化注釋的時候，你可能需要看一下它的手冊。
不過記住： ``indent`` 不能修正壞的編程習慣。


10) Kconfig 配置文件
------------------------------

對於遍布源碼樹的所有 Kconfig* 配置文件來說，它們縮進方式有所不同。緊挨著
``config`` 定義的行，用一個制表符縮進，然而 help 信息的縮進則額外增加 2 個空
格。舉個例子::

  config AUDIT
	bool "Auditing support"
	depends on NET
	help
	  Enable auditing infrastructure that can be used with another
	  kernel subsystem, such as SELinux (which requires this for
	  logging of avc messages output).  Does not do system-call
	  auditing without CONFIG_AUDITSYSCALL.

而那些危險的功能 (比如某些文件系統的寫支持) 應該在它們的提示字符串里顯著的聲
明這一點::

  config ADFS_FS_RW
	bool "ADFS write support (DANGEROUS)"
	depends on ADFS_FS
	...

要查看配置文件的完整文檔，請看 Documentation/kbuild/kconfig-language.rst。


11) 數據結構
------------------------------

如果一個數據結構，在創建和銷毀它的單線執行環境之外可見，那麼它必須要有一個引
用計數器。內核里沒有垃圾收集 (並且內核之外的垃圾收集慢且效率低下)，這意味著你
絕對需要記錄你對這種數據結構的使用情況。

引用計數意味著你能夠避免上鎖，並且允許多個用戶並行訪問這個數據結構——而不需要
擔心這個數據結構僅僅因爲暫時不被使用就消失了，那些用戶可能不過是沉睡了一陣或
者做了一些其他事情而已。

注意上鎖 **不能** 取代引用計數。上鎖是爲了保持數據結構的一致性，而引用計數是一
個內存管理技巧。通常二者都需要，不要把兩個搞混了。

很多數據結構實際上有 2 級引用計數，它們通常有不同 ``類`` 的用戶。子類計數器統
計子類用戶的數量，每當子類計數器減至零時，全局計數器減一。

這種 ``多級引用計數`` 的例子可以在內存管理 (``struct mm_struct``: mm_users 和
mm_count)，和文件系統 (``struct super_block``: s_count 和 s_active) 中找到。

記住：如果另一個執行線索可以找到你的數據結構，但這個數據結構沒有引用計數器，
這裡幾乎肯定是一個 bug。


12) 宏，枚舉和RTL
------------------------------

用於定義常量的宏的名字及枚舉里的標籤需要大寫。

.. code-block:: c

	#define CONSTANT 0x12345

在定義幾個相關的常量時，最好用枚舉。

宏的名字請用大寫字母，不過形如函數的宏的名字可以用小寫字母。

一般的，如果能寫成內聯函數就不要寫成像函數的宏。

含有多個語句的宏應該被包含在一個 do-while 代碼塊里：

.. code-block:: c

	#define macrofun(a, b, c)			\
		do {					\
			if (a == 5)			\
				do_this(b, c);		\
		} while (0)

使用宏的時候應避免的事情：

1) 影響控制流程的宏：

.. code-block:: c

	#define FOO(x)					\
		do {					\
			if (blah(x) < 0)		\
				return -EBUGGERED;	\
		} while (0)

**非常** 不好。它看起來像一個函數，不過卻能導致 ``調用`` 它的函數退出；不要打
亂讀者大腦里的語法分析器。

2) 依賴於一個固定名字的本地變量的宏：

.. code-block:: c

	#define FOO(val) bar(index, val)

可能看起來像是個不錯的東西，不過它非常容易把讀代碼的人搞糊塗，而且容易導致看起
來不相關的改動帶來錯誤。

3) 作爲左值的帶參數的宏： FOO(x) = y；如果有人把 FOO 變成一個內聯函數的話，這
   種用法就會出錯了。

4) 忘記了優先級：使用表達式定義常量的宏必須將表達式置於一對小括號之內。帶參數
   的宏也要注意此類問題。

.. code-block:: c

	#define CONSTANT 0x4000
	#define CONSTEXP (CONSTANT | 3)

5) 在宏里定義類似函數的本地變量時命名衝突：

.. code-block:: c

	#define FOO(x)				\
	({					\
		typeof(x) ret;			\
		ret = calc_ret(x);		\
		(ret);				\
	})

ret 是本地變量的通用名字 - __foo_ret 更不容易與一個已存在的變量衝突。

cpp 手冊對宏的講解很詳細。gcc internals 手冊也詳細講解了 RTL，內核里的彙編語
言經常用到它。


13) 列印內核消息
------------------------------

內核開發者應該是受過良好教育的。請一定注意內核信息的拼寫，以給人以好的印象。
不要用不規範的單詞比如 ``dont``，而要用 ``do not`` 或者 ``don't`` 。保證這些信
息簡單明了,無歧義。

內核信息不必以英文句號結束。

在小括號里列印數字 (%d) 沒有任何價值，應該避免這樣做。

<linux/device.h> 里有一些驅動模型診斷宏，你應該使用它們，以確保信息對應於正確
的設備和驅動，並且被標記了正確的消息級別。這些宏有：dev_err(), dev_warn(),
dev_info() 等等。對於那些不和某個特定設備相關連的信息，<linux/printk.h> 定義
了 pr_notice(), pr_info(), pr_warn(), pr_err() 和其他。

寫出好的調試信息可以是一個很大的挑戰；一旦你寫出後，這些信息在遠程除錯時能提
供極大的幫助。然而列印調試信息的處理方式同列印非調試信息不同。其他 pr_XXX()
函數能無條件地列印，pr_debug() 卻不；默認情況下它不會被編譯，除非定義了 DEBUG
或設定了 CONFIG_DYNAMIC_DEBUG。實際這同樣是爲了 dev_dbg()，一個相關約定是在一
個已經開啓了 DEBUG 時，使用 VERBOSE_DEBUG 來添加 dev_vdbg()。

許多子系統擁有 Kconfig 調試選項來開啓 -DDEBUG 在對應的 Makefile 裡面；在其他
情況下，特殊文件使用 #define DEBUG。當一條調試信息需要被無條件列印時，例如，
如果已經包含一個調試相關的 #ifdef 條件，printk(KERN_DEBUG ...) 就可被使用。


14) 分配內存
------------------------------

內核提供了下面的一般用途的內存分配函數：
kmalloc(), kzalloc(), kmalloc_array(), kcalloc(), vmalloc() 和 vzalloc()。
請參考 API 文檔以獲取有關它們的詳細信息。

傳遞結構體大小的首選形式是這樣的：

.. code-block:: c

	p = kmalloc(sizeof(*p), ...);

另外一種傳遞方式中，sizeof 的操作數是結構體的名字，這樣會降低可讀性，並且可能
會引入 bug。有可能指針變量類型被改變時，而對應的傳遞給內存分配函數的 sizeof
的結果不變。

強制轉換一個 void 指針返回值是多餘的。C 語言本身保證了從 void 指針到其他任何
指針類型的轉換是沒有問題的。

分配一個數組的首選形式是這樣的：

.. code-block:: c

	p = kmalloc_array(n, sizeof(...), ...);

分配一個零長數組的首選形式是這樣的：

.. code-block:: c

	p = kcalloc(n, sizeof(...), ...);

兩種形式檢查分配大小 n * sizeof(...) 的溢出，如果溢出返回 NULL。


15) 內聯弊病
------------------------------

有一個常見的誤解是 ``內聯`` 是 gcc 提供的可以讓代碼運行更快的一個選項。雖然使
用內聯函數有時候是恰當的 (比如作爲一種替代宏的方式，請看第十二章)，不過很多情
況下不是這樣。inline 的過度使用會使內核變大，從而使整個系統運行速度變慢。
因爲體積大內核會占用更多的指令高速緩存，而且會導致 pagecache 的可用內存減少。
想像一下，一次 pagecache 未命中就會導致一次磁碟尋址，將耗時 5 毫秒。5 毫秒的
時間內 CPU 能執行很多很多指令。

一個基本的原則是如果一個函數有 3 行以上，就不要把它變成內聯函數。這個原則的一
個例外是，如果你知道某個參數是一個編譯時常量，而且因爲這個常量你確定編譯器在
編譯時能優化掉你的函數的大部分代碼，那仍然可以給它加上 inline 關鍵字。
kmalloc() 內聯函數就是一個很好的例子。

人們經常主張給 static 的而且只用了一次的函數加上 inline，如此不會有任何損失，
因爲沒有什麼好權衡的。雖然從技術上說這是正確的，但是實際上這種情況下即使不加
inline gcc 也可以自動使其內聯。而且其他用戶可能會要求移除 inline，由此而來的
爭論會抵消 inline 自身的潛在價值，得不償失。


16) 函數返回值及命名
------------------------------

函數可以返回多種不同類型的值，最常見的一種是表明函數執行成功或者失敗的值。這樣
的一個值可以表示爲一個錯誤代碼整數 (-Exxx＝失敗，0＝成功) 或者一個 ``成功``
布爾值 (0＝失敗，非0＝成功)。

混合使用這兩種表達方式是難於發現的 bug 的來源。如果 C 語言本身嚴格區分整形和
布爾型變量，那麼編譯器就能夠幫我們發現這些錯誤... 不過 C 語言不區分。爲了避免
產生這種 bug，請遵循下面的慣例::

	如果函數的名字是一個動作或者強制性的命令，那麼這個函數應該返回錯誤代
	碼整數。如果是一個判斷，那麼函數應該返回一個 "成功" 布爾值。

比如， ``add work`` 是一個命令，所以 add_work() 在成功時返回 0，在失敗時返回
-EBUSY。類似的，因爲 ``PCI device present`` 是一個判斷，所以 pci_dev_present()
在成功找到一個匹配的設備時應該返回 1，如果找不到時應該返回 0。

所有 EXPORTed 函數都必須遵守這個慣例，所有的公共函數也都應該如此。私有
(static) 函數不需要如此，但是我們也推薦這樣做。

返回值是實際計算結果而不是計算是否成功的標誌的函數不受此慣例的限制。一般的，
他們通過返回一些正常值範圍之外的結果來表示出錯。典型的例子是返回指針的函數，
他們使用 NULL 或者 ERR_PTR 機制來報告錯誤。


17) 不要重新發明內核宏
------------------------------

頭文件 include/linux/kernel.h 包含了一些宏，你應該使用它們，而不要自己寫一些
它們的變種。比如，如果你需要計算一個數組的長度，使用這個宏

.. code-block:: c

	#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

類似的，如果你要計算某結構體成員的大小，使用

.. code-block:: c

	#define sizeof_field(t, f) (sizeof(((t*)0)->f))

還有可以做嚴格的類型檢查的 min() 和 max() 宏，如果你需要可以使用它們。你可以
自己看看那個頭文件里還定義了什麼你可以拿來用的東西，如果有定義的話，你就不應
在你的代碼里自己重新定義。


18) 編輯器模式行和其他需要羅嗦的事情
--------------------------------------------------

有一些編輯器可以解釋嵌入在源文件里的由一些特殊標記標明的配置信息。比如，emacs
能夠解釋被標記成這樣的行：

.. code-block:: c

	-*- mode: c -*-

或者這樣的：

.. code-block:: c

	/*
	Local Variables:
	compile-command: "gcc -DMAGIC_DEBUG_FLAG foo.c"
	End:
	*/

Vim 能夠解釋這樣的標記：

.. code-block:: c

	/* vim:set sw=8 noet */

不要在原始碼中包含任何這樣的內容。每個人都有他自己的編輯器配置，你的源文件不
應該覆蓋別人的配置。這包括有關縮進和模式配置的標記。人們可以使用他們自己定製
的模式，或者使用其他可以產生正確的縮進的巧妙方法。


19) 內聯彙編
------------------------------

在特定架構的代碼中，你可能需要內聯彙編與 CPU 和平台相關功能連接。需要這麼做時
就不要猶豫。然而，當 C 可以完成工作時，不要平白無故地使用內聯彙編。在可能的情
況下，你可以並且應該用 C 和硬體溝通。

請考慮去寫捆綁通用位元 (wrap common bits) 的內聯彙編的簡單輔助函數，別去重複
地寫下只有細微差異內聯彙編。記住內聯彙編可以使用 C 參數。

大型，有一定複雜度的彙編函數應該放在 .S 文件內，用相應的 C 原型定義在 C 頭文
件中。彙編函數的 C 原型應該使用 ``asmlinkage`` 。

你可能需要把彙編語句標記爲 volatile，用來阻止 GCC 在沒發現任何副作用後就把它
移除了。你不必總是這樣做，儘管，這不必要的舉動會限制優化。

在寫一個包含多條指令的單個內聯彙編語句時，把每條指令用引號分割而且各占一行，
除了最後一條指令外，在每個指令結尾加上 \n\t，讓彙編輸出時可以正確地縮進下一條
指令：

.. code-block:: c

	asm ("magic %reg1, #42\n\t"
	     "more_magic %reg2, %reg3"
	     : /* outputs */ : /* inputs */ : /* clobbers */);


20) 條件編譯
------------------------------

只要可能，就不要在 .c 文件裡面使用預處理條件 (#if, #ifdef)；這樣做讓代碼更難
閱讀並且更難去跟蹤邏輯。替代方案是，在頭文件中用預處理條件提供給那些 .c 文件
使用，再給 #else 提供一個空樁 (no-op stub) 版本，然後在 .c 文件內無條件地調用
那些 (定義在頭文件內的) 函數。這樣做，編譯器會避免爲樁函數 (stub) 的調用生成
任何代碼，產生的結果是相同的，但邏輯將更加清晰。

最好傾向於編譯整個函數，而不是函數的一部分或表達式的一部分。與其放一個 ifdef
在表達式內，不如分解出部分或全部表達式，放進一個單獨的輔助函數，並應用預處理
條件到這個輔助函數內。

如果你有一個在特定配置中，可能變成未使用的函數或變量，編譯器會警告它定義了但
未使用，把它標記爲 __maybe_unused 而不是將它包含在一個預處理條件中。(然而，如
果一個函數或變量總是未使用，就直接刪除它。)

在代碼中，儘可能地使用 IS_ENABLED 宏來轉化某個 Kconfig 標記爲 C 的布爾
表達式，並在一般的 C 條件中使用它：

.. code-block:: c

	if (IS_ENABLED(CONFIG_SOMETHING)) {
		...
	}

編譯器會做常量摺疊，然後就像使用 #ifdef 那樣去包含或排除代碼塊，所以這不會帶
來任何運行時開銷。然而，這種方法依舊允許 C 編譯器查看塊內的代碼，並檢查它的正
確性 (語法，類型，符號引用，等等)。因此，如果條件不滿足，代碼塊內的引用符號就
不存在時，你還是必須去用 #ifdef。

在任何有意義的 #if 或 #ifdef 塊的末尾 (超過幾行的)，在 #endif 同一行的後面寫下
註解，注釋這個條件表達式。例如：

.. code-block:: c

	#ifdef CONFIG_SOMETHING
	...
	#endif /* CONFIG_SOMETHING */


附錄 I) 參考
-------------------

The C Programming Language, 第二版
作者：Brian W. Kernighan 和 Denni M. Ritchie.
Prentice Hall, Inc., 1988.
ISBN 0-13-110362-8 (軟皮), 0-13-110370-9 (硬皮).

The Practice of Programming
作者：Brian W. Kernighan 和 Rob Pike.
Addison-Wesley, Inc., 1999.
ISBN 0-201-61586-X.

GNU 手冊 - 遵循 K&R 標準和此文本 - cpp, gcc, gcc internals and indent,
都可以從 https://www.gnu.org/manual/ 找到

WG14 是 C 語言的國際標準化工作組，URL: http://www.open-std.org/JTC1/SC22/WG14/

Kernel process/coding-style.rst，作者 greg@kroah.com 發表於 OLS 2002：
http://www.kroah.com/linux/talks/ols_2002_kernel_codingstyle_talk/html/

