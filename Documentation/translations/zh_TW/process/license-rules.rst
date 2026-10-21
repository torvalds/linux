.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/license-rules.rst <kernel_licensing>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>
             Hu Haowen <2023002089@link.tyut.edu.cn>
             Chen-Yu Yeh <chenyou910331@gmail.com>

.. _tw_kernel_licensing:

Linux核心許可規則
=================

Linux核心根據LICENSES/preferred/GPL-2.0中提供的GNU通用公共許可證版本2
（GPL-2.0）的條款提供，並在LICENSES/exceptions/Linux-syscall-note中顯式
描述了例外的系統呼叫，如COPYING檔案中所述。

本文件提供了如何對每個原始檔進行註解以使其許可證清晰明確的說明。
它不會取代核心的許可證。

核心原始程式碼作為一個整體適用於COPYING檔案中描述的許可證，但是單個原始檔可以
具有不同的與GPL-2.0相容的許可證::

    GPL-1.0+ : GNU通用公共許可證v1.0或更高版本
    GPL-2.0+ : GNU通用公共許可證v2.0或更高版本
    LGPL-2.0 : 僅限GNU庫通用公共許可證v2
    LGPL-2.0+: GNU 庫通用公共許可證v2或更高版本
    LGPL-2.1 : 僅限GNU寬通用公共許可證v2.1
    LGPL-2.1+: GNU寬通用公共許可證v2.1或更高版本

除此之外，個人檔案可以在雙重許可下提供，例如一個相容的GPL變體，或者BSD，
MIT等許可。

使用者空間API（UAPI）標頭檔描述了使用者空間程式與核心的介面，這是一種特殊
情況。根據核心COPYING檔案中的註解，syscall介面是一個明確的邊界，它不會將
GPL要求擴展到任何使用它與核心通信的軟體。由於UAPI標頭檔必須包含在建立在
Linux核心上執行的可執行檔案的任何原始檔中，因此此例外必須記錄在特別的許可
證表述中。

表達原始檔許可證的常用方法是將匹配的樣板文本添加到檔案的頂部註解中。由於
格式，拼寫錯誤等，這些“樣板”很難透過那些在上下文中使用的驗證許可證合規性
的工具。

樣板文本的替代方法是在每個原始檔中使用軟體套件資料交換（SPDX）許可證識別碼。
SPDX許可證識別碼是機器可解析的，並且是用於提供檔案內容的許可證的精確縮寫。
SPDX許可證識別碼由Linux 基金會的SPDX 工作組管理，並得到了整個行業，工具
供應商和法律團隊的合作夥伴的一致同意。有關詳細資訊，請參閱
https://spdx.org/

Linux核心需要所有原始檔中的精確SPDX識別碼。核心中使用的有效識別碼在
`許可識別碼`_ 一節中進行了解釋，並且已可以在
https://spdx.org/licenses/ 上的官方SPDX許可證列表中檢索，並附帶許可證
文本。

許可識別碼語法
--------------

1.安置:

   核心檔案中的SPDX許可證識別碼應添加到可包含註解的檔案中的第一行。對於大多
   數檔案，這是第一行，除了那些在第一行中需要'#!PATH_TO_INTERPRETER'的腳本。
   對於這些腳本，SPDX許可證識別碼進入第二行。

   如有需要，許可證識別碼行之後可以接上一行或多行SPDX-FileCopyrightText
   行。

|

2. 風格:

   SPDX許可證識別碼以註解的形式添加。註解樣式取決於檔案類型::

      C source:	// SPDX-License-Identifier: <SPDX License Expression>
      C header:	/* SPDX-License-Identifier: <SPDX License Expression> */
      ASM:	/* SPDX-License-Identifier: <SPDX License Expression> */
      scripts:	# SPDX-License-Identifier: <SPDX License Expression>
      .rst:	.. SPDX-License-Identifier: <SPDX License Expression>
      .dts{i}:	// SPDX-License-Identifier: <SPDX License Expression>

   如果特定工具無法處理標準註解樣式，則應使用工具接受的相應註解機制。這是在
   C 標頭檔中使用“/\*\*/”樣式註解的原因。過去在使用產生的.lds檔案中觀察到
   建置被破壞，其中'ld'無法解析C++註解。現在已經解決了這個問題，但仍然有較
   舊的組譯器工具無法處理C++樣式的註解。

|

3. 句法:

   <SPDX許可證表達式>是SPDX許可證列表中的SPDX短格式許可證識別碼，或者在許可
   證例外適用時由“WITH”分隔的兩個SPDX短格式許可證識別碼的組合。當應用多個許
   可證時，表達式由分隔子表達式的關鍵字“AND”，“OR”組成，並由“（”，“）”包圍。

   帶有“或更高”選項的[L]GPL等許可證的許可證識別碼透過使用“+”來表示“或更高”
   選項來建置。::

      // SPDX-License-Identifier: GPL-2.0+
      // SPDX-License-Identifier: LGPL-2.1+

   當需要修正的許可證時，應使用WITH。 例如，linux核心UAPI檔案使用表達式::

      // SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
      // SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note

   其它在核心中使用WITH例外的事例如下::

      // SPDX-License-Identifier: GPL-2.0 WITH mif-exception
      // SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   例外只能與特定的許可證識別碼一起使用。有效的許可證識別碼列在異常文本檔案
   的標記中。有關詳細資訊，請參閱 `許可識別碼`_ 一章中的 `例外`_ 。

   如果檔案是雙重許可且只選擇一個許可證，則應使用OR。例如，一些dtsi檔案在雙
   許可下可用::

      // SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

   核心中雙許可檔案中許可表達式的範例::

      // SPDX-License-Identifier: GPL-2.0 OR MIT
      // SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
      // SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
      // SPDX-License-Identifier: GPL-2.0 OR MPL-1.1
      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT
      // SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause OR OpenSSL

   如果檔案具有多個許可證，其條款全部適用於使用該檔案，則應使用AND。例如，
   如果程式碼是從另一個專案繼承的，並且已經授予了將其放入核心的權限，但原始
   許可條款需要保持有效::

      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) AND MIT

   另一個需要遵守兩套許可條款的例子是::

      // SPDX-License-Identifier: GPL-1.0+ AND LGPL-2.1+

許可識別碼
----------

當前使用的許可證以及添加到核心的程式碼許可證可以分解為：

1. _`優先許可`:

   應儘可能使用這些許可證，因為它們已知完全相容並廣泛使用。這些許可證在核心
   目錄::

      LICENSES/preferred/

   此目錄中的檔案包含完整的許可證文本和 `元標記`_ 。檔名與SPDX許可證識別碼
   相同，後者應用於原始檔中的許可證。

   例如::

      LICENSES/preferred/GPL-2.0

   包含GPLv2許可證文本和所需的元標籤::

      LICENSES/preferred/MIT

   包含MIT許可證文本和所需的元標記

   _`元標記`:

   許可證檔案中必須包含以下元標記：

   - Valid-License-Identifier:

     一行或多行, 聲明那些許可識別碼在專案內有效, 以引用此特定許可的文本。通
     常這是一個有效的識別碼，但是例如對於帶有'或更高'選項的許可證，兩個識別碼
     符都有效。

   - SPDX-URL:

     SPDX頁面的URL，其中包含與許可證相關的其他資訊.

   - Usage-Guidance:

     使用建議的自由格式文本。該文本必須包含SPDX許可證識別碼的正確範例，因為
     它們應根據 `許可識別碼語法`_ 指南放入原始檔中。

   - License-Text:

     此標記之後的所有文本都被視為原始許可文本

   檔案格式範例::

      Valid-License-Identifier: GPL-2.0
      Valid-License-Identifier: GPL-2.0+
      SPDX-URL: https://spdx.org/licenses/GPL-2.0.html
      Usage-Guide:
        To use this license in source code, put one of the following SPDX
	tag/value pairs into a comment according to the placement
	guidelines in the licensing rules documentation.
	For 'GNU General Public License (GPL) version 2 only' use:
	  SPDX-License-Identifier: GPL-2.0
	For 'GNU General Public License (GPL) version 2 or any later version' use:
	  SPDX-License-Identifier: GPL-2.0+
      License-Text:
        Full license text

   ::

      SPDX-License-Identifier: MIT
      SPDX-URL: https://spdx.org/licenses/MIT.html
      Usage-Guide:
	To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: MIT
      License-Text:
        Full license text

|

2. 不推薦的許可證:

   這些許可證只應用於現有程式碼或從其他專案導入程式碼。這些許可證在核心目錄::

      LICENSES/other/

   此目錄中的檔案包含完整的許可證文本和 `元標記`_ 。檔名與SPDX許可證識別碼
   相同，後者應用於原始檔中的許可證。

   例如::

      LICENSES/other/ISC

   包含國際系統聯合許可文本和所需的元標籤::

      LICENSES/other/ZLib

   包含ZLIB許可文本和所需的元標籤.

   元標籤:

   “其他”許可證的元標籤要求與 `優先許可`_ 的要求相同。

   檔案格式範例::

      Valid-License-Identifier: ISC
      SPDX-URL: https://spdx.org/licenses/ISC.html
      Usage-Guide:
        Usage of this license in the kernel for new code is discouraged
	and it should solely be used for importing code from an already
	existing project.
        To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: ISC
      License-Text:
        Full license text

|

3. _`例外`:

   某些許可證可以修改，並允許原始許可證不具有的某些例外權利。這些例外在
   核心目錄::

      LICENSES/exceptions/

   此目錄中的檔案包含完整的例外文本和所需的 `例外元標記`_ 。

   例如::

      LICENSES/exceptions/Linux-syscall-note

   包含Linux核心的COPYING檔案中記錄的Linux系統呼叫例外，該檔案用於UAPI
   標頭檔。例如::

      LICENSES/exceptions/GCC-exception-2.0

   包含GCC'連結例外'，它允許獨立於其許可證的任何二進位檔案與標記有此例外的
   檔案的編譯版本連結。這是從GPL不相容原始程式碼建立可執行的可執行檔案所必需的。

   _`例外元標記`:

   以下元標記必須在例外檔案中可用：

   - SPDX-Exception-Identifier:

     一個可與SPDX許可證識別碼一起使用的例外識別碼。

   - SPDX-URL:

     SPDX頁面的URL，其中包含與例外相關的其他資訊。

   - SPDX-Licenses:

     以逗號分隔的例外可用的SPDX許可證識別碼列表。

   - Usage-Guidance:

     使用建議的自由格式文本。必須在文本後面加上SPDX許可證識別碼的正確範例，
     因為它們應根據 `許可識別碼語法`_ 指南放入原始檔中。

   - Exception-Text:

     此標記之後的所有文本都被視為原始異常文本

   檔案格式範例::

      SPDX-Exception-Identifier: Linux-syscall-note
      SPDX-URL: https://spdx.org/licenses/Linux-syscall-note.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+
      Usage-Guidance:
        This exception is used together with one of the above SPDX-Licenses
	to mark user-space API (uapi) header files so they can be included
	into non GPL compliant user-space application code.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-note
      Exception-Text:
        Full exception text

   ::

      SPDX-Exception-Identifier: GCC-exception-2.0
      SPDX-URL: https://spdx.org/licenses/GCC-exception-2.0.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+
      Usage-Guidance:
        The "GCC Runtime Library exception 2.0" is used together with one
	of the above SPDX-Licenses for code imported from the GCC runtime
	library.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH GCC-exception-2.0
      Exception-Text:
        Full exception text


所有SPDX許可證識別碼和例外都必須在LICENSES子目錄中具有相應的檔案。這是允許
工具驗證（例如checkpatch.pl）以及準備好從源讀取和提取許可證所必需的, 這是
各種FOSS組織推薦的，例如 `FSFE REUSE initiative <https://reuse.software/>`_.

_`模組許可`
-----------------

   可載入核心模組還需要MODULE_LICENSE（）標記。此標記既不替代正確的原始程式碼
   許可證資訊（SPDX-License-Identifier），也不以任何方式表示或確定提供模組
   原始程式碼的確切許可證。

   此標記的唯一目的是提供足夠的資訊，該模組是否是自由軟體或者是核心模組加
   載器和使用者空間工具的專有模組。

   MODULE_LICENSE（）的有效許可證字串是:

    ============================= =============================================
    "GPL"			  模組是根據GPL版本2許可的。這並不表示僅限於
                                  GPL-2.0或GPL-2.0或更高版本之間的任何區別。
                                  最正確許可證資訊只能透過相應原始檔中的許可證
                                  資訊來確定

    "GPL v2"			  和"GPL"相同，它的存在是因為歷史原因。

    "GPL and additional rights"   表示模組源在GPL v2變體和MIT許可下雙重許可的
                                  歷史變體。請不要在新程式碼中使用。

    "Dual MIT/GPL"		  表達該模組在GPL v2變體或MIT許可證選擇下雙重
                                  許可的正確方式。

    "Dual BSD/GPL"		  該模組根據GPL v2變體或BSD許可證選擇進行雙重
                                  許可。 BSD許可證的確切變體只能透過相應原始檔
                                  中的許可證資訊來確定。

    "Dual MPL/GPL"		  該模組根據GPL v2變體或Mozilla Public License
                                  （MPL）選項進行雙重許可。 MPL許可證的確切變體
                                  只能透過相應的原始檔中的許可證資訊來確定。

    "Proprietary"		  該模組屬於非GPL2相容的許可。“Proprietary
                                  （專有）”應僅理解為“該許可證與GPLv2不相容”。
                                  此字串僅用於非GPL2相容的第三方模組，不能用
                                  於在核心樹中具有原始程式碼的模組。以這種方式
                                  標記的模組在載入時會使用'P'標記污染核心，並
                                  且核心模組載入器拒絕將這些模組連結到使用
                                  EXPORT_SYMBOL_GPL（）匯出的符號。
    ============================= =============================================


