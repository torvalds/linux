.. _z3fold:

======
z3fold
======

z3fold is a special purpose allocator for storing compressed pages.
It is designed to store up to three compressed pages per physical page.
It is a zbud derivative which allows for higher compression
ratio keeping the simplicity and determinism of its predecessor.

The main differences between z3fold and zbud are:

* unlike zbud, z3fold allows for up to PAGE_SIZE allocations
* z3fold can hold up to 3 compressed pages in its page
* z3fold doesn't export any API itself and is thus intended to be used
  via the zpool API.

To keep the determinism and simplicity, z3fold, just like zbud, always
stores an integral number of compressed pages per page, but it can store
up to 3 pages unlike zbud which can store at most 2. Therefore the
compression ratio goes to around 2.7x while zbud's one is around 1.7x.

Unlike zbud (but like zsmalloc for that matter) z3fold_alloc() does not
return a dereferenceable pointer. Instead, it returns an unsigned long
handle which encodes actual location of the allocated object.

Keeping effective compression ratio close to zsmalloc's, z3fold doesn't
depend on MMU enabled and provides more predictable reclaim behavior
which makes it a better fit for small and response-critical systems.
