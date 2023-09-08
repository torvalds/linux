.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Carlos Bilbao <carlos.bilbao@amd.com>

.. _sp_programming_language:

Lenguaje de programación
========================

El kernel está escrito en el lenguaje de programación C [sp-c-language]_.
Más concretamente, el kernel normalmente se compila con ``gcc`` [sp-gcc]_
bajo ``-std=gnu11`` [sp-gcc-c-dialect-options]_: el dialecto GNU de ISO C11.
``clang`` [sp-clang]_ también es compatible, consulte los documentos en
:ref:`Building Linux with Clang/LLVM <kbuild_llvm>`.

Este dialecto contiene muchas extensiones del lenguaje [sp-gnu-extensions]_,
y muchos de ellos se usan dentro del kernel de forma habitual.

Hay algo de soporte para compilar el núcleo con ``icc`` [sp-icc]_ para varias
de las arquitecturas, aunque en el momento de escribir este texto, eso no
está terminado y requiere parches de terceros.

Atributos
---------

Una de las comunes extensiones utilizadas en todo el kernel son los atributos
[sp-gcc-attribute-syntax]_. Los atributos permiten introducir semántica
definida por la implementación a las entidades del lenguaje (como variables,
funciones o tipos) sin tener que hacer cambios sintácticos significativos
al idioma (por ejemplo, agregar una nueva palabra clave) [sp-n2049]_.

En algunos casos, los atributos son opcionales (es decir, hay compiladores
que no los admiten pero de todos modos deben producir el código adecuado,
incluso si es más lento o no realiza tantas comprobaciones/diagnósticos en
tiempo de compilación).

El kernel define pseudo-palabras clave (por ejemplo, ``__pure``) en lugar
de usar directamente la sintaxis del atributo GNU (por ejemplo,
``__attribute__((__pure__))``) con el fin de detectar cuáles se pueden
utilizar y/o acortar el código.

Por favor consulte ``include/linux/compiler_attributes.h`` para obtener
más información.

.. [sp-c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [sp-gcc] https://gcc.gnu.org
.. [sp-clang] https://clang.llvm.org
.. [sp-icc] https://software.intel.com/en-us/c-compilers
.. [sp-gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [sp-gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [sp-gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [sp-n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf
