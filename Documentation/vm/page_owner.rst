.. _page_owner:

==================================================
page owner: Tracking about who allocated each page
==================================================

Introduction
============

page owner is for the tracking about who allocated each page.
It can be used to debug memory leak or to find a memory hogger.
When allocation happens, information about allocation such as call stack
and order of pages is stored into certain storage for each page.
When we need to know about status of all pages, we can get and analyze
this information.

Although we already have tracepoint for tracing page allocation/free,
using it for analyzing who allocate each page is rather complex. We need
to enlarge the trace buffer for preventing overlapping until userspace
program launched. And, launched program continually dump out the trace
buffer for later analysis and it would change system behviour with more
possibility rather than just keeping it in memory, so bad for debugging.

page owner can also be used for various purposes. For example, accurate
fragmentation statistics can be obtained through gfp flag information of
each page. It is already implemented and activated if page owner is
enabled. Other usages are more than welcome.

page owner is disabled in default. So, if you'd like to use it, you need
to add "page_owner=on" into your boot cmdline. If the kernel is built
with page owner and page owner is disabled in runtime due to no enabling
boot option, runtime overhead is marginal. If disabled in runtime, it
doesn't require memory to store owner information, so there is no runtime
memory overhead. And, page owner inserts just two unlikely branches into
the page allocator hotpath and if not enabled, then allocation is done
like as the kernel without page owner. These two unlikely branches should
not affect to allocation performance, especially if the static keys jump
label patching functionality is available. Following is the kernel's code
size change due to this facility.

- Without page owner::

   text    data     bss     dec     hex filename
   40662   1493     644   42799    a72f mm/page_alloc.o

- With page owner::

   text    data     bss     dec     hex filename
   40892   1493     644   43029    a815 mm/page_alloc.o
   1427      24       8    1459     5b3 mm/page_ext.o
   2722      50       0    2772     ad4 mm/page_owner.o

Although, roughly, 4 KB code is added in total, page_alloc.o increase by
230 bytes and only half of it is in hotpath. Building the kernel with
page owner and turning it on if needed would be great option to debug
kernel memory problem.

There is one notice that is caused by implementation detail. page owner
stores information into the memory from struct page extension. This memory
is initialized some time later than that page allocator starts in sparse
memory system, so, until initialization, many pages can be allocated and
they would have no owner information. To fix it up, these early allocated
pages are investigated and marked as allocated in initialization phase.
Although it doesn't mean that they have the right owner information,
at least, we can tell whether the page is allocated or not,
more accurately. On 2GB memory x86-64 VM box, 13343 early allocated pages
are catched and marked, although they are mostly allocated from struct
page extension feature. Anyway, after that, no page is left in
un-tracking state.

Usage
=====

1) Build user-space helper::

	cd tools/vm
	make page_owner_sort

2) Enable page owner: add "page_owner=on" to boot cmdline.

3) Do the job what you want to debug

4) Analyze information from page owner::

	cat /sys/kernel/debug/page_owner > page_owner_full.txt
	./page_owner_sort page_owner_full.txt sorted_page_owner.txt

   See the result about who allocated each page
   in the ``sorted_page_owner.txt``.
