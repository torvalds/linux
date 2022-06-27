:Original: Documentation/vm/z3fold.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


======
z3fold
======

z3fold是一个专门用于存储压缩页的分配器。它被设计为每个物理页最多可以存储三个压缩页。
它是zbud的衍生物，允许更高的压缩率，保持其前辈的简单性和确定性。

z3fold和zbud的主要区别是:

* 与zbud不同的是，z3fold允许最大的PAGE_SIZE分配。
* z3fold在其页面中最多可以容纳3个压缩页面
* z3fold本身没有输出任何API，因此打算通过zpool的API来使用

为了保持确定性和简单性，z3fold，就像zbud一样，总是在每页存储一个整数的压缩页，但是
它最多可以存储3页，不像zbud最多可以存储2页。因此压缩率达到2.7倍左右，而zbud的压缩
率是1.7倍左右。

不像zbud（但也像zsmalloc），z3fold_alloc()那样不返回一个可重复引用的指针。相反，它
返回一个无符号长句柄，它编码了被分配对象的实际位置。

保持有效的压缩率接近于zsmalloc，z3fold不依赖于MMU的启用，并提供更可预测的回收行
为，这使得它更适合于小型和反应迅速的系统。
