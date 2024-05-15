:Original: Documentation/mm/page_frags.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


========
页面片段
========

一个页面片段是一个任意长度的任意偏移的内存区域，它位于一个0或更高阶的复合页面中。
该页中的多个碎片在该页的引用计数器中被单独计算。

page_frag函数，page_frag_alloc和page_frag_free，为页面片段提供了一个简单
的分配框架。这被网络堆栈和网络设备驱动使用，以提供一个内存的支持区域，作为
sk_buff->head使用，或者用于skb_shared_info的 “frags” 部分。

为了使用页面片段API，需要一个支持页面片段的缓冲区。这为碎片分配提供了一个中心点，
并允许多个调用使用一个缓存的页面。这样做的好处是可以避免对get_page的多次调用，
这在分配时开销可能会很大。然而，由于这种缓存的性质，要求任何对缓存的调用都要受到每
个CPU的限制，或者每个CPU的限制，并在执行碎片分配时强制禁止中断。

网络堆栈在每个CPU使用两个独立的缓存来处理碎片分配。netdev_alloc_cache被使用
netdev_alloc_frag和__netdev_alloc_skb调用的调用者使用。napi_alloc_cache
被调用__napi_alloc_frag和napi_alloc_skb的调用者使用。这两个调用的主要区别是
它们可能被调用的环境。“netdev” 前缀的函数可以在任何上下文中使用，因为这些函数
将禁用中断，而 ”napi“ 前缀的函数只可以在softirq上下文中使用。

许多网络设备驱动程序使用类似的方法来分配页面片段，但页面片段是在环或描述符级别上
缓存的。为了实现这些情况，有必要提供一种拆解页面缓存的通用方法。出于这个原因，
__page_frag_cache_drain被实现了。它允许通过一次调用从一个页面释放多个引用。
这样做的好处是，它允许清理被添加到一个页面的多个引用，以避免每次分配都调用
get_page。

Alexander Duyck，2016年11月29日。
