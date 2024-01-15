.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/unicode.rst

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <src.res.211@gmail.com>

Unicode（統一碼）支持
======================

	（英文版）上次更新：2005-01-17，版本號 1.4

此文檔由H. Peter Anvin <unicode@lanana.org>管理，是Linux註冊名稱與編號管理局
（Linux Assigned Names And Numbers Authority，LANANA）項目的一部分。
現行版本請見：

	http://www.lanana.org/docs/unicode/admin-guide/unicode.rst

簡介
-----

Linux內核代碼已被重寫以使用Unicode來將字符映射到字體。下載一個Unicode到字體
（Unicode-to-font）表，八位字符集與UTF-8模式都將改用此字體來顯示。

這微妙地改變了八位字符表的語義。現在的四個字符表是：

=============== =============================== ================
映射代號        映射名稱                        Escape代碼 (G0)
=============== =============================== ================
LAT1_MAP        Latin-1 (ISO 8859-1)            ESC ( B
GRAF_MAP        DEC VT100 pseudographics        ESC ( 0
IBMPC_MAP       IBM code page 437               ESC ( U
USER_MAP        User defined                    ESC ( K
=============== =============================== ================

特別是 ESC ( U 不再是“直通字體”，因爲字體可能與IBM字符集完全不同。
例如，即使加載了一個Latin-1字體，也允許使用塊圖形（block graphics）。

請注意，儘管這些代碼與ISO 2022類似，但這些代碼及其用途都與ISO 2022不匹配；
Linux有兩個八位代碼（G0和G1），而ISO 2022有四個七位代碼（G0-G3）。

根據Unicode標準/ISO 10646，U+F000到U+F8FF被保留用於操作系統範圍內的分配
（Unicode標準將其稱爲“團體區域（Corporate Zone）”，因爲這對於Linux是不準確
的，所以我們稱之爲“Linux區域”）。選擇U+F000作爲起點，因爲它允許直接映射
區域以2的大倍數開始（以防需要1024或2048個字符的字體）。這就留下U+E000到
U+EFFF作爲最終用戶區。

[v1.2]：Unicodes範圍從U+F000到U+F7FF已經被硬編碼爲直接映射到加載的字體，
繞過了翻譯表。用戶定義的映射現在默認爲U+F000到U+F0FF，模擬前述行爲。實際上，
此範圍可能較短；例如，vgacon只能處理256字符（U+F000..U+F0FF）或512字符
（U+F000..U+F1FF）字體。

Linux 區域中定義的實際字符
---------------------------

此外，還定義了Unicode 1.1.4中不存在的以下字符；這些字符由DEC VT圖形映射使用。
[v1.2]此用法已過時，不應再使用；請參見下文。

====== ======================================
U+F800 DEC VT GRAPHICS HORIZONTAL LINE SCAN 1
U+F801 DEC VT GRAPHICS HORIZONTAL LINE SCAN 3
U+F803 DEC VT GRAPHICS HORIZONTAL LINE SCAN 7
U+F804 DEC VT GRAPHICS HORIZONTAL LINE SCAN 9
====== ======================================

DEC VT220使用6x10字符矩陣，這些字符在DEC VT圖形字符集中形成一個平滑的過渡。
我省略了掃描5行，因爲它也被用作塊圖形字符，因此被編碼爲U+2500 FORMS LIGHT
HORIZONTAL。

[v1.3]：這些字符已正式添加到Unicode 3.2.0中；它們在U+23BA、U+23BB、U+23BC、
U+23BD處添加。Linux現在使用新值。

[v1.2]：添加了以下字符來表示常見的鍵盤符號，這些符號不太可能被添加到Unicode
中，因爲它們非常討厭地取決於特定供應商。當然，這是糟糕設計的一個好例子。

====== ======================================
U+F810 KEYBOARD SYMBOL FLYING FLAG
U+F811 KEYBOARD SYMBOL PULLDOWN MENU
U+F812 KEYBOARD SYMBOL OPEN APPLE
U+F813 KEYBOARD SYMBOL SOLID APPLE
====== ======================================

克林貢（Klingon）語支持
------------------------

1996年，Linux是世界上第一個添加對人工語言克林貢支持的操作系統，克林貢是由
Marc Okrand爲《星際迷航》電視連續劇創造的。這種編碼後來被徵募Unicode註冊表
（ConScript Unicode Registry，CSUR）採用，並建議（但最終被拒絕）納入Unicode
平面一。不過，它仍然是Linux區域中的Linux/CSUR私有分配。

這種編碼已經得到克林貢語言研究所（Klingon Language Institute）的認可。
有關更多信息，請聯繫他們：

	http://www.kli.org/

由於Linux CZ開頭部分的字符大多是dingbats/symbols/forms類型，而且這是一種
語言，因此根據標準Unicode慣例，我將它放置在16單元的邊界上。

.. note::

  這個範圍現在由徵募Unicode註冊表正式管理。規範性引用文件爲：

	https://www.evertype.com/standards/csur/klingon.html

克林貢語有一個26個字符的字母表，一個10位數的位置數字書寫系統，從左到右
，從上到下書寫。

克林貢字母的幾種字形已經被提出。但是由於這組符號看起來始終是一致的，只有實際
的形狀不同，因此按照標準Unicode慣例，這些差異被認爲是字體變體。

======	=======================================================
U+F8D0	KLINGON LETTER A
U+F8D1	KLINGON LETTER B
U+F8D2	KLINGON LETTER CH
U+F8D3	KLINGON LETTER D
U+F8D4	KLINGON LETTER E
U+F8D5	KLINGON LETTER GH
U+F8D6	KLINGON LETTER H
U+F8D7	KLINGON LETTER I
U+F8D8	KLINGON LETTER J
U+F8D9	KLINGON LETTER L
U+F8DA	KLINGON LETTER M
U+F8DB	KLINGON LETTER N
U+F8DC	KLINGON LETTER NG
U+F8DD	KLINGON LETTER O
U+F8DE	KLINGON LETTER P
U+F8DF	KLINGON LETTER Q
	- Written <q> in standard Okrand Latin transliteration
U+F8E0	KLINGON LETTER QH
	- Written <Q> in standard Okrand Latin transliteration
U+F8E1	KLINGON LETTER R
U+F8E2	KLINGON LETTER S
U+F8E3	KLINGON LETTER T
U+F8E4	KLINGON LETTER TLH
U+F8E5	KLINGON LETTER U
U+F8E6	KLINGON LETTER V
U+F8E7	KLINGON LETTER W
U+F8E8	KLINGON LETTER Y
U+F8E9	KLINGON LETTER GLOTTAL STOP

U+F8F0	KLINGON DIGIT ZERO
U+F8F1	KLINGON DIGIT ONE
U+F8F2	KLINGON DIGIT TWO
U+F8F3	KLINGON DIGIT THREE
U+F8F4	KLINGON DIGIT FOUR
U+F8F5	KLINGON DIGIT FIVE
U+F8F6	KLINGON DIGIT SIX
U+F8F7	KLINGON DIGIT SEVEN
U+F8F8	KLINGON DIGIT EIGHT
U+F8F9	KLINGON DIGIT NINE

U+F8FD	KLINGON COMMA
U+F8FE	KLINGON FULL STOP
U+F8FF	KLINGON SYMBOL FOR EMPIRE
======	=======================================================

其他虛構和人工字母
-------------------

自從分配了克林貢Linux Unicode塊之後，John Cowan <jcowan@reutershealth.com>
和 Michael Everson <everson@evertype.com> 建立了一個虛構和人工字母的註冊表。
徵募Unicode註冊表請訪問：

	https://www.evertype.com/standards/csur/

所使用的範圍位於最終用戶區域的低端，因此無法進行規範化分配，但建議希望對虛構
字母進行編碼的人員使用這些代碼，以實現互操作性。對於克林貢語，CSUR採用了Linux
編碼。CSUR的人正在推動將Tengwar和Cirth添加到Unicode平面一；將克林貢添加到
Unicode平面一被拒絕，因此上述編碼仍然是官方的。

