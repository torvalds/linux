.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/deprecated.rst <deprecated>`
:Translator: Sergio Gonzalez <sergio.collado@gmail.com>

.. _sp_deprecated:

============================================================================
Interfaces obsoletos, Características del lenguaje, Atributos y Convenciones
============================================================================

En un mundo perfecto, sería posible convertir todas las instancias de
alguna API obsoleta en una nueva API y quitar la API anterior en un
único ciclo de desarrollo. Desafortunadamente, debido al tamaño del kernel,
la jerarquía de mantenimiento, y el tiempo, no siempre es posible hacer
estos cambios de una única vez. Esto significa que las nuevas instancias
han de ir creándose en el kernel, mientras que las antiguas se quitan,
haciendo que la cantidad de trabajo para limpiar las APIs crezca. Para
informar a los desarrolladores sobre qué ha sido declarado obsoleto y por
qué, ha sido creada esta lista como un lugar donde indicar cuando los usos
obsoletos son propuestos para incluir en el kernel.

__deprecated
------------
Mientras que este atributo señala visualmente que un interface ha sido
declarado obsoleto, este `no produce más avisos durante las compilaciones
<https://git.kernel.org/linus/771c035372a036f83353eef46dbb829780330234>`_
porque uno de los objetivos del kernel es que compile sin avisos, y
nadie ha hecho nada para quitar estos interfaces obsoletos. Mientras
que usar `__deprecated` es sencillo para anotar una API obsoleta en
un archivo de cabecera, no es la solución completa. Dichos interfaces
deben o bien ser quitados por completo, o añadidos a este archivo para
desanimar a otros a usarla en el futuro.

BUG() y BUG_ON()
----------------
Use WARN() y WARN_ON() en su lugar, y gestione las condiciones de error
"imposibles" tan elegantemente como se pueda. Mientras que la familia de
funciones BUG() fueron originalmente diseñadas para actuar como una
"situación imposible", confirmar y disponer de un hilo del kernel de forma
"segura", estas funciones han resultado ser demasiado arriesgadas. (e.g.
"¿en qué orden se necesitan liberar los locks? ¿Se han restaurado sus
estados?). La popular función BUG() desestabilizará el sistema o lo romperá
totalmente, lo cual hace imposible depurarlo o incluso generar reportes de
crash. Linus tiene una `opinión muy fuerte
<https://lore.kernel.org/lkml/CA+55aFy6jNLsywVYdGp83AMrXBo_P-pkjkphPGrO=82SPKCpLQ@mail.gmail.com/>`_
y sentimientos `sobre esto
<https://lore.kernel.org/lkml/CAHk-=whDHsbK3HTOpTF=ue_o04onRwTEaK_ZoJp_fjbqq4+=Jw@mail.gmail.com/>`_.

Nótese que la familia de funciones WARN() únicamente debería ser usada
en situaciones que se "esperan no sean alcanzables". Si se quiere
avisar sobre situaciones "alcanzables pero no deseadas", úsese la familia
de funciones pr_warn(). Los responsables del sistema pueden haber definido
*panic_on_warn* sysctl para asegurarse que sus sistemas no continúan
ejecutándose en presencia del condiciones "no alcanzables". (Por ejemplo,
véase commits como `este
<https://git.kernel.org/linus/d4689846881d160a4d12a514e991a740bcb5d65a>`_.)

Operaciones aritméticas en los argumentos de reserva de memoria
---------------------------------------------------------------
Los cálculos dinámicos de tamaño (especialmente multiplicaciones) no
deberían realizarse en los argumentos de reserva de memoria (o similares)
debido al riesgo de desbordamiento. Esto puede llevar a valores rotando y
que se realicen reservas de memoria menores que las que se esperaban. El
uso de esas reservas puede llevar a desbordamientos en el 'heap' de memoria
y otros funcionamientos incorrectos. (Una excepción a esto son los valores
literales donde el compilador si puede avisar si estos puede desbordarse.
De todos modos, el método recomendado en estos caso es reescribir el código
como se sugiere a continuación para evitar las operaciones aritméticas en
la reserva de memoria.)

Por ejemplo, no utilice `count * size`` como argumento, como en::

    foo = kmalloc(count * size, GFP_KERNEL);

En vez de eso, utilice la reserva con dos argumentos::

    	foo = kmalloc_array(count, size, GFP_KERNEL);

Específicamente, kmalloc() puede ser sustituido con kmalloc_array(),
kzalloc() puede ser sustituido con kcalloc().

Si no existen funciones con dos argumentos, utilice las funciones que se
saturan, en caso de desbordamiento::

    	bar = vmalloc(array_size(count, size));

Otro caso común a evitar es calcular el tamaño de una estructura com
la suma de otras estructuras, como en::

    header = kzalloc(sizeof(*header) + count * sizeof(*header->item),
   		  GFP_KERNEL);

En vez de eso emplee::

    header = kzalloc(struct_size(header, item, count), GFP_KERNEL);

.. note:: Si se usa struct_size() en una estructura que contiene un elemento
    	de longitud cero o un array de un único elemento como un array miembro,
    	por favor reescribir ese uso y cambiar a un `miembro array flexible
    	<#zero-length-and-one-element-arrays>`_


Para otros cálculos, por favor use las funciones de ayuda: size_mul(),
size_add(), and size_sub(). Por ejemplo, en el caso de::

    foo = krealloc(current_size + chunk_size * (count - 3), GFP_KERNEL);

Re-escríbase, como::

    foo = krealloc(size_add(current_size,
   			 size_mul(chunk_size,
   				  size_sub(count, 3))), GFP_KERNEL);

Para más detalles, mire también array3_size() y flex_array_size(),
como también la familia de funciones relacionadas check_mul_overflow(),
check_add_overflow(), check_sub_overflow(), y check_shl_overflow().


simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
Las funciones: simple_strtol(), simple_strtoll(), simple_strtoul(), y
simple_strtoull() explícitamente ignoran los desbordamientos, lo que puede
llevar a resultados inesperados por las funciones que las llaman. Las
funciones respectivas kstrtol(), kstrtoll(), kstrtoul(), y kstrtoull()
tienden a ser reemplazos correctos, aunque nótese que necesitarán que la
cadena de caracteres termine en NUL o en el carácter de línea nueva.


strcpy()
--------
strcpy() no realiza verificaciones de los límites del buffer de destino.
Esto puede resultar en desbordamientos lineals más allá del fin del buffer,
causando todo tipo de errores. Mientras `CONFIG_FORTIFY_SOURCE=y` otras
varias opciones de compilación reducen el riesgo de usar esta función, no
hay ninguna buena razón para añadir nuevos usos de esta. El remplazo seguro
es la función strscpy(), aunque se ha de tener cuidado con cualquier caso
en el el valor retornado por strcpy() sea usado, ya que strscpy() no
devuelve un puntero a el destino, sino el número de caracteres no nulos
compilados (o el valor negativo de errno cuando se trunca la cadena de
caracteres).

strncpy() en cadenas de caracteres terminadas en NUL
----------------------------------------------------
El uso de strncpy() no garantiza que el buffer de destino esté terminado en
NUL. Esto puede causar varios errores de desbordamiento en lectura y otros
tipos de funcionamiento erróneo debido a que falta la terminación en NUL.
Esta función también termina la cadena de caracteres en NUL en el buffer de
destino si la cadena de origen es más corta que el buffer de destino, lo
cual puede ser una penalización innecesaria para funciones usen esta
función con cadenas de caracteres que sí están terminadas en NUL.

Cuando se necesita que la cadena de destino sea terminada en NUL,
el mejor reemplazo es usar la función strscpy(), aunque se ha de tener
cuidado en los casos en los que el valor de strncpy() fuera usado, ya que
strscpy() no devuelve un puntero al destino, sino el número de
caracteres no nulos copiados (o el valor negativo de errno cuando se trunca
la cadena de caracteres). Cualquier caso restante que necesitase todavía
ser terminado en el caracter nulo, debería usar strscpy_pad().

Si una función usa cadenas de caracteres que no necesitan terminar en NUL,
debería usarse strtomem(), y el destino debería señalarse con el atributo
`__nonstring
<https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>`_
para evitar avisos futuros en el compilador. Para casos que todavía
necesitan cadenas de caracteres que se rellenen al final con el
caracter NUL, usar strtomem_pad().

strlcpy()
---------
strlcpy() primero lee por completo el buffer de origen (ya que el valor
devuelto intenta ser el mismo que el de strlen()). Esta lectura puede
sobrepasar el límite de tamaño del destino. Esto ineficiente y puede causar
desbordamientos de lectura si la cadena de origen no está terminada en el
carácter NUL. El reemplazo seguro de esta función es strscpy(), pero se ha
de tener cuidado que en los casos en lso que se usase el valor devuelto de
strlcpy(), ya que strscpy() devolverá valores negativos de erno cuando se
produzcan truncados.

Especificación de formato %p
----------------------------
Tradicionalmente,el uso de "%p" en el formato de cadenas de caracteres
resultaría en exponer esas direcciones en dmesg, proc, sysfs, etc. En vez
de dejar que sean una vulnerabilidad, todos los "%p" que se usan en el
kernel se imprimen como un hash, haciéndolos efectivamente inutilizables
para usarlos como direcciones de memoria. Nuevos usos de "%p" no deberían
ser añadidos al kernel. Para textos de direcciones, usar "%pS" es
mejor, ya que resulta en el nombre del símbolo. Para prácticamente el
resto de casos, mejor no usar "%p" en absoluto.

Parafraseando las actuales `direcciones de Linus <https://lore.kernel.org/lkml/CA+55aFwQEd_d40g4mUCSsVRZzrFPUJt74vc6PPpb675hYNXcKw@mail.gmail.com/>`_:

- Si el valor "hasheado" "%p" no tienen ninguna finalidad, preguntarse si el
  puntero es realmente importante. ¿Quizás se podría quitar totalmente?
- Si realmente se piensa que el valor del puntero es importante, ¿porqué
  algún estado del sistema o nivel de privilegio de usuario es considerado
  "especial"? Si piensa que puede justificarse (en comentarios y mensajes
  del commit), de forma suficiente como para pasar el escrutinio de Linux,
  quizás pueda usar el "%p", a la vez que se asegura que tiene los permisos
  correspondientes.

Si está depurando algo donde el "%p" hasheado está causando problemas,
se puede arrancar temporalmente con la opción de depuración "`no_hash_pointers
<https://git.kernel.org/linus/5ead723a20e0447bc7db33dc3070b420e5f80aa6>`_".


Arrays de longitud variable (VLAs)
----------------------------------
Usando VLA en la pila (stack) produce un código mucho peor que los arrays
de tamaño estático. Mientras que estos errores no triviales de `rendimiento
<https://git.kernel.org/linus/02361bc77888>`_  son razón suficiente
para no usar VLAs, esto además son un riesgo de seguridad. El crecimiento
dinámico del array en la pila, puede exceder la memoria restante en
el segmento de la pila. Esto podría llevara a un fallo, posible sobre-escritura
de contenido al final de la pila (cuando se construye sin
`CONFIG_THREAD_INFO_IN_TASK=y`), o sobre-escritura de la memoria adyacente
a la pila (cuando se construye sin `CONFIG_VMAP_STACK=y`).


Switch case fall-through implícito
----------------------------------
El lenguaje C permite a las sentencias 'switch' saltar de un caso al
siguiente caso cuando la sentencia de ruptura "break" no aparece al final
del caso. Esto, introduce ambigüedad en el código, ya que no siempre está
claro si el 'break' que falta es intencionado o un olvido. Por ejemplo, no
es obvio solamente mirando al código si `STATE_ONE` está escrito para
intencionadamente saltar en `STATE_TWO`::

    switch (value) {
    case STATE_ONE:
   	 do_something();
    case STATE_TWO:
   	 do_other();
   	 break;
    default:
   	 WARN("unknown state");
    }

Ya que ha habido una larga lista de defectos `debidos a declaraciones de "break"
que faltan <https://cwe.mitre.org/data/definitions/484.html>`_, no se
permiten 'fall-through' implícitos. Para identificar 'fall-through'
intencionados, se ha adoptado la pseudo-palabra-clave macro "falltrhrough",
que expande las extensiones de gcc `__attribute__((__fallthrough__))
<https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html>`_.
(Cuando la sintaxis de C17/c18 `[[fallthrough]]` sea más comúnmente
soportadas por los compiladores de C, analizadores estáticos, e IDEs,
se puede cambiar a usar esa sintaxis para esa pseudo-palabra-clave.

Todos los bloques switch/case deben acabar en uno de:

* break;
* fallthrough;
* continue;
* goto <label>;
* return [expression];


Arrays de longitud cero y un elemento
-------------------------------------
Hay una necesidad habitual en el kernel de proveer una forma para declarar
un grupo de elementos consecutivos de tamaño dinámico en una estructura.
El código del kernel debería usar siempre `"miembros array flexible" <https://en.wikipedia.org/wiki/Flexible_array_member>`_
en estos casos. El estilo anterior de arrays de un elemento o de longitud
cero, no deben usarse más.

En el código C más antiguo, los elementos finales de tamaño dinámico se
obtenían especificando un array de un elemento al final de una estructura::

    	struct something {
            	size_t count;
            	struct foo items[1];
    	};

En código C más antiguo, elementos seguidos de tamaño dinámico eran creados
especificando una array de un único elemento al final de una estructura::

    	struct something {
            	size_t count;
            	struct foo items[1];
    	};

Esto llevó a resultados incorrectos en los cálculos de tamaño mediante
sizeof() (el cual hubiera necesitado eliminar el tamaño del último elemento
para tener un tamaño correcto de la "cabecera"). Una `extensión de GNU C
<https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_ se empezó a usar
para permitir los arrays de longitud cero, para evitar estos tipos de
problemas de tamaño::

    	struct something {
            	size_t count;
            	struct foo items[0];
    	};

Pero esto llevó a otros problemas, y no solucionó algunos otros problemas
compartidos por ambos estilos, como no ser capaz de detectar cuando ese array
accidentalmente _no_ es usado al final de la estructura (lo que podía pasar
directamente, o cuando dicha estructura era usada en uniones, estructuras
de estructuras, etc).

C99 introdujo "los arrays miembros flexibles", los cuales carecen de un
tamaño numérico en su declaración del array::

    	struct something {
            	size_t count;
            	struct foo items[];
    	};

Esta es la forma en la que el kernel espera que se declaren los elementos
de tamaño dinámico concatenados. Esto permite al compilador generar
errores, cuando el array flexible no es declarado en el último lugar de la
estructura, lo que ayuda a prevenir errores en él código del tipo
`comportamiento indefinido <https://git.kernel.org/linus/76497732932f15e7323dc805e8ea8dc11bb587cf>`_.
Esto también permite al compilador analizar correctamente los tamaños de
los arrays (via sizeof(), `CONFIG_FORTIFY_SOURCE`, y `CONFIG_UBSAN_BOUNDS`).
Por ejemplo, si no hay un mecanismo que avise que el siguiente uso de
sizeof() en un array de longitud cero, siempre resulta en cero::

        struct something {
                size_t count;
                struct foo items[0];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count), GFP_KERNEL);
        instance->count = count;

        size = sizeof(instance->items) * instance->count;
        memcpy(instance->items, source, size);

En la última línea del código anterior, ``zero`` vale ``cero``, cuando uno
podría esperar que representa el tamaño total en bytes de la memoria dinámica
reservada para el array consecutivo ``items``. Aquí hay un par de ejemplos
más sobre este tema:  `link 1
<https://git.kernel.org/linus/f2cd32a443da694ac4e28fbf4ac6f9d5cc63a539>`_,
`link 2
<https://git.kernel.org/linus/ab91c2a89f86be2898cee208d492816ec238b2cf>`_.
Sin embargo, los array de miembros flexibles tienen un type incompleto, y
no se ha de aplicar el operador sizeof()<https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_,
así cualquier mal uso de dichos operadores será detectado inmediatamente en
el momento de compilación.

Con respecto a los arrays de un único elemento, se ha de ser consciente de
que dichos arrays ocupan al menos tanto espacio como un único objeto del
tipo https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_, de ahí que
estos contribuyan al tamaño de la estructura que los contiene. Esto es
proclive a errores cada vez que se quiere calcular el tamaño total de la
memoria dinámica para reservar una estructura que contenga un array de este
tipo como su miembro::

        struct something {
                size_t count;
                struct foo items[1];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count - 1), GFP_KERNEL);
        instance->count = count;

        size = sizeof(instance->items) * instance->count;
        memcpy(instance->items, source, size);

En el ejemplo anterior, hemos de recordar calcular ``count - 1``, cuando se
usa la función de ayuda struct_size(), de otro modo estaríamos
--desintencionadamente--reservando memoria para un  ``items`` de más. La
forma más clara y menos proclive a errores es implementar esto mediante el
uso de `array miembro flexible`, junto con las funciones de ayuda:
struct_size() y flex_array_size()::

        struct something {
                size_t count;
                struct foo items[];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count), GFP_KERNEL);
        instance->count = count;

        memcpy(instance->items, source, flex_array_size(instance, items, instance->count));
