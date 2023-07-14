.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/coding-style.rst <submittingpatches>`
:Translator: Carlos Bilbao <carlos.bilbao@amd.com>

.. _sp_codingstyle:

Estilo en el código del kernel Linux
=====================================

Este es un breve documento que describe el estilo preferido en el código
del kernel Linux. El estilo de código es muy personal y no **forzaré** mi
puntos de vista sobre nadie, pero esto vale para todo lo que tengo que
mantener, y preferiría que para la mayoría de otras cosas también. Por
favor, por lo menos considere los argumentos expuestos aquí.

En primer lugar, sugeriría imprimir una copia de los estándares de código
GNU, y NO leerlo. Quémelos, es un gran gesto simbólico.

De todos modos, aquí va:


1) Sangría
-----------

Las tabulaciones tienen 8 caracteres y, por lo tanto, las sangrías también
tienen 8 caracteres. Hay movimientos heréticos que intentan hacer sangría
de 4 (¡o incluso 2!) caracteres de longitud, y eso es similar a tratar de
definir el valor de PI como 3.

Justificación: La idea detrás de la sangría es definir claramente dónde
comienza y termina un bloque de control. Especialmente, cuando ha estado
buscando en su pantalla durante 20 horas seguidas, le resultará mucho más
fácil ver cómo funciona la sangría si tiene sangrías grandes.

Bueno, algunas personas dirán que tener sangrías de 8 caracteres hace que
el código se mueva demasiado a la derecha y dificulta la lectura en una
pantalla de terminal de 80 caracteres. La respuesta a eso es que si
necesita más de 3 niveles de sangría, está en apuros de todos modos y
debería arreglar su programa.

En resumen, las sangrías de 8 caracteres facilitan la lectura y tienen la
ventaja añadida de advertirle cuando está anidando sus funciones demasiado
profundo. Preste atención a esa advertencia.

La forma preferida de facilitar múltiples niveles de sangría en una
declaración de switch es para alinear el ``switch`` y sus etiquetas
``case`` subordinadas en la misma columna, en lugar de hacer ``doble
sangría`` (``double-indenting``) en etiquetas ``case``. Por ejemplo:

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

No ponga varias declaraciones en una sola línea a menos que tenga algo que
ocultar:

.. code-block:: c

	if (condición) haz_esto;
	  haz_otra_cosa;

No use comas para evitar el uso de llaves:

.. code-block:: c

	if (condición)
		haz_esto(), haz_eso();

Siempre use llaves para múltiples declaraciones:

.. code-block:: c

	if (condición) {
		haz_esto();
		haz_eso();
	}

Tampoco ponga varias asignaciones en una sola línea. El estilo de código
del kernel es súper simple. Evite las expresiones engañosas.


Aparte de los comentarios, la documentación y excepto en Kconfig, los
espacios nunca se utilizan para la sangría, y el ejemplo anterior se rompe
deliberadamente.

Consiga un editor decente y no deje espacios en blanco al final de las
líneas.

2) Rompiendo líneas y strings largos
------------------------------------

El estilo de código tiene todo que ver con la legibilidad y la
mantenibilidad usando herramientas disponibles comúnmente.

El límite preferido en la longitud de una sola línea es de 80 columnas.

Las declaraciones de más de 80 columnas deben dividirse en partes, a menos
que exceder las 80 columnas aumente significativamente la legibilidad y no
oculte información.

Los descendientes siempre son sustancialmente más cortos que el padre y
se colocan sustancialmente a la derecha. Un estilo muy usado es alinear
descendientes a un paréntesis de función abierto.

Estas mismas reglas se aplican a los encabezados de funciones con una larga
lista de argumentos.

Sin embargo, nunca rompa los strings visibles para el usuario, como los
mensajes printk, porque eso rompe la capacidad de grep a estos.


3) Colocación de llaves y espacios
----------------------------------

El otro problema que siempre surge en el estilo C es la colocación de
llaves. A diferencia del tamaño de la sangría, existen pocas razones
técnicas para elegir una estrategia de ubicación sobre la otra, pero la
forma preferida, como mostraron los profetas Kernighan y Ritchie, es poner
la llave de apertura en la línea, y colocar la llave de cierre primero,
así:

.. code-block:: c

	if (x es verdad) {
		hacemos y
	}

Esto se aplica a todos los bloques de declaraciones que no son funciones
(if, switch, for, while, do). Por ejemplo:

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

Sin embargo, hay un caso especial, a saber, las funciones: tienen la llave
de apertura al comienzo de la siguiente línea, así:

.. code-block:: c

	int funcion(int x)
	{
		cuerpo de la función
	}

Gente hereje de todo el mundo ha afirmado que esta inconsistencia es...
bueno... inconsistente, pero todas las personas sensatas saben que
(a) K&R tienen **razón** y (b) K&R tienen razón. Además, las funciones son
especiales de todos modos (no puede anidarlas en C).

Tenga en cuenta que la llave de cierre está vacía en su línea propia,
**excepto** en los casos en que es seguida por una continuación de la misma
declaración, es decir, un ``while`` en una sentencia do o un ``else`` en
una sentencia if, como en:

.. code-block:: c

	do {
		cuerpo del bucle do
	} while (condition);

y

.. code-block:: c

	if (x == y) {
		..
	} else if (x > y) {
		...
	} else {
		....
	}

Justificación: K&R.

Además, tenga en cuenta que esta colocación de llaves también minimiza el
número de líneas vacías (o casi vacías), sin pérdida de legibilidad. Así,
como el suministro de nuevas líneas en su pantalla no es un recurso
renovable (piense en pantallas de terminal de 25 líneas), tienes más líneas
vacías para poner comentarios.

No use llaves innecesariamente donde una sola declaración sea suficiente.

.. code-block:: c

	if (condition)
		accion();

y

.. code-block:: none

	if (condición)
		haz_esto();
	else
		haz_eso();

Esto no aplica si solo una rama de una declaración condicional es una sola
declaración; en este último caso utilice llaves en ambas ramas:

.. code-block:: c

	if (condición) {
		haz_esto();
		haz_eso();
	} else {
		en_otro_caso();
	}

Además, use llaves cuando un bucle contenga más de una declaración simple:

.. code-block:: c

	while (condición) {
		if (test)
			haz_eso();
	}

3.1) Espacios
*************

El estilo del kernel Linux para el uso de espacios depende (principalmente)
del uso de función versus uso de palabra clave. Utilice un espacio después
de (la mayoría de) las palabras clave. Las excepciones notables son sizeof,
typeof, alignof y __attribute__, que parecen algo así como funciones (y
generalmente se usan con paréntesis en Linux, aunque no son requeridos en
el idioma, como en: ``sizeof info`` después de que ``struct fileinfo info;``
se declare).

Así que use un espacio después de estas palabras clave::

	if, switch, case, for, do, while

pero no con sizeof, typeof, alignof, o __attribute__. Por ejemplo,

.. code-block:: c


	s = sizeof(struct file);

No agregue espacios alrededor (dentro) de expresiones entre paréntesis.
Este ejemplo es **malo**:

.. code-block:: c


	s = sizeof( struct file );

Al declarar datos de puntero o una función que devuelve un tipo de puntero,
el uso preferido de ``*`` es adyacente al nombre del dato o nombre de la
función y no junto al nombre del tipo. Ejemplos:

.. code-block:: c


	char *linux_banner;
	unsigned long long memparse(char *ptr, char **retptr);
	char *match_strdup(substring_t *s);

Use un espacio alrededor (a cada lado de) la mayoría de los operadores
binarios y ternarios, como cualquiera de estos::

	=  +  -  <  >  *  /  %  |  &  ^  <=  >=  ==  !=  ?  :

pero sin espacio después de los operadores unarios::

	&  *  +  -  ~  !  sizeof  typeof  alignof  __attribute__  defined

sin espacio antes de los operadores unarios de incremento y decremento del
sufijo::

	++  --

y sin espacio alrededor de los operadores de miembros de estructura ``.`` y
``->``.

No deje espacios en blanco al final de las líneas. Algunos editores con
``inteligente`` sangría insertarán espacios en blanco al comienzo de las
nuevas líneas como sea apropiado, para que pueda comenzar a escribir la
siguiente línea de código de inmediato. Sin embargo, algunos de estos
editores no eliminan los espacios en blanco si finalmente no termina
poniendo una línea de código allí, como si dejara una línea en blanco. Como
resultado, termina con líneas que contienen espacios en blanco al final.

Git le advertirá sobre los parches que introducen espacios en blanco al
final y puede, opcionalmente, eliminar los espacios en blanco finales por
usted; sin embargo, si se aplica una serie de parches, esto puede hacer que
los parches posteriores de la serie fallen al cambiar sus líneas de
contexto.


4) Nomenclatura
---------------

C es un lenguaje espartano, y sus convenciones de nomenclatura deberían
seguir su ejemplo. A diferencia de los programadores de Modula-2 y Pascal,
los programadores de C no usan nombres cuquis como
EstaVariableEsUnContadorTemporal. Un programador de C lo llamaría
variable ``tmp``, que es mucho más fácil de escribir, y no es mas difícil
de comprender.

SIN EMBARGO, mientras que los nombres de mayúsculas y minúsculas están mal
vistos, los nombres descriptivos para las variables globales son
imprescindibles. Llamar a una función global ``foo`` es un delito.

Una variable GLOBAL (para usar solo si **realmente** las necesita) necesita
tener un nombre descriptivo, al igual que las funciones globales. Si tiene
una función que cuenta el número de usuarios activos, debe llamar a esta
``contar_usuarios_activos()`` o similar, **no** debe llamarlo ``cntusr()``.

Codificar el tipo de una función en el nombre (lo llamado notación húngara)
es estúpido: el compilador conoce los tipos de todos modos y puede
verificar estos, y solo confunde al programador.

Los nombres de las variables LOCALES deben ser breves y directos. Si usted
tiene algún contador aleatorio de tipo entero, probablemente debería
llamarse ``i``. Llamarlo ``loop_counter`` no es productivo, si no hay
posibilidad de ser mal entendido. De manera similar, ``tmp`` puede ser casi
cualquier tipo de variable que se utiliza para contener un valor temporal.

Si tiene miedo de mezclar los nombres de las variables locales, tiene otro
problema, que se denomina síndrome de
función-crecimiento-desequilibrio-de-hormona. Vea el capítulo 6 (Funciones).

Para nombres de símbolos y documentación, evite introducir nuevos usos de
'master / slave' (maestro / esclavo) (o 'slave' independientemente de
'master') y 'lista negra / lista blanca' (backlist / whitelist).

Los reemplazos recomendados para 'maestro / esclavo' son:
    '{primary,main} / {secondary,replica,subordinate}'
    '{initiator,requester} / {target,responder}'
    '{controller,host} / {device,worker,proxy}'
    'leader / follower'
    'director / performer'

Los reemplazos recomendados para 'backlist / whitelist' son:
    'denylist / allowlist'
    'blocklist / passlist'

Las excepciones para la introducción de nuevos usos son mantener en espacio
de usuario una ABI/API, o al actualizar la especificación del código de un
hardware o protocolo existente (a partir de 2020) que requiere esos
términos. Para nuevas especificaciones, traduzca el uso de la terminología
de la especificación al estándar de código del kernel donde sea posible.

5) Typedefs
-----------

Por favor no use cosas como ``vps_t``.
Es un **error** usar typedef para estructuras y punteros. cuando ve un

.. code-block:: c


	vps_t a;

en el código fuente, ¿qué significa?
En cambio, si dice

.. code-block:: c

	struct virtual_container *a;

puede decir qué es ``a`` en realidad.

Mucha gente piensa que  los typedefs ``ayudan a la legibilidad``. No. Son
útiles solamente para:

 (a) objetos totalmente opacos (donde el typedef se usa activamente para
     **ocultar** cuál es el objeto).

     Ejemplo: ``pte_t`` etc. objetos opacos a los que solo puede acceder
     usando las funciones de acceso adecuadas.

     .. note::

       La opacidad y las ``funciones de acceso`` no son buenas por sí
       mismas. La razón por la que los tenemos para cosas como pte_t, etc.
       es que hay real y absolutamente **cero** información accesible de
       forma portátil allí.

 (b) Tipos enteros claros, donde la abstracción **ayuda** a evitar
     confusiones, ya sea ``int`` o ``long``.

     u8/u16/u32 son definiciones tipográficas perfectamente correctas
     aunque encajan en la categoría (d) mejor que aquí.

     .. note::

       De nuevo - debe haber una **razón** para esto. si algo es
       ``unsigned long``, entonces no hay razón para hacerlo

	typedef unsigned long mis_flags_t;

     pero si hay una razón clara de por qué bajo ciertas circunstancias
     podría ser un ``unsigned int`` y bajo otras configuraciones podría
     ser ``unsigned long``, entonces, sin duda, adelante y use un typedef.

 (c) cuando lo use para crear literalmente un tipo **nuevo** para
     comprobación de tipos.

 (d) Nuevos tipos que son idénticos a los tipos estándar C99, en ciertas
     circunstancias excepcionales.

     Aunque sólo costaría un corto período de tiempo para los ojos y
     cerebro para acostumbrarse a los tipos estándar como ``uint32_t``,
     algunas personas se oponen a su uso de todos modos.

     Por lo tanto, los tipos ``u8/u16/u32/u64`` específicos de Linux y sus
     equivalentes con signo, que son idénticos a los tipos estándar son
     permitidos, aunque no son obligatorios en el nuevo código de su
     elección.

     Al editar código existente que ya usa uno u otro conjunto de tipos,
     debe ajustarse a las opciones existentes en ese código.

 (e) Tipos seguros para usar en el espacio de usuario.

     En ciertas estructuras que son visibles para el espacio de usuario, no
     podemos requerir tipos C99 y o utilizat el ``u32`` anterior. Por lo
     tanto, usamos __u32 y tipos similares en todas las estructuras que se
     comparten con espacio de usuario.

Tal vez también haya otros casos, pero la regla básicamente debería ser
NUNCA JAMÁS use un typedef a menos que pueda coincidir claramente con una
de estas reglas.

En general, un puntero o una estructura que tiene elementos que pueden
ser razonablemente accedidos directamente, **nunca** deben ser un typedef.

6) Funciones
------------

Las funciones deben ser cortas y dulces, y hacer una sola cosa. Deberían
caber en una o dos pantallas de texto (el tamaño de pantalla ISO/ANSI es
80x24, como todos sabemos), y hacer una cosa y hacerla bien.

La longitud máxima de una función es inversamente proporcional a la
complejidad y el nivel de sangría de esa función. Entonces, si tiene una
función conceptualmente simple que es solo una larga (pero simple)
declaración de case, donde tiene que hacer un montón de pequeñas cosas para
un montón de diferentes casos, está bien tener una función más larga.

Sin embargo, si tiene una función compleja y sospecha que un estudiante de
primer año de secundaria menos que dotado podría no comprender de qué se
trata la función, debe adherirse a los límites máximos tanto más de
cerca. Use funciones auxiliares con nombres descriptivos (puede pedirle al
compilador que los alinee si cree que es crítico para el rendimiento, y
probablemente lo hará mejor de lo que usted hubiera hecho).

Otra medida de la función es el número de variables locales. Estas no deben
exceder de 5 a 10, o está haciendo algo mal. Piense de nuevo en la función
y divida en partes más pequeñas. Un cerebro humano puede generalmente
realiza un seguimiento de aproximadamente 7 cosas diferentes, cualquier
elemento más y se confunde. Usted sabe que es brillante, pero tal vez le
gustaría entender lo que hizo dentro de 2 semanas.

En los archivos fuente, separe las funciones con una línea en blanco. Si la
función es exportada, la macro **EXPORT** debería ponerse inmediatamente
después de la función de cierre de línea de llave. Por ejemplo:

.. code-block:: c

	int sistema_corriendo(void)
	{
		return estado_sistema == SISTEMA_CORRIENDO;
	}
	EXPORT_SYMBOL(sistema_corriendo);

6.1) Prototipos de funciones
****************************

En los prototipos de funciones, incluya nombres de parámetros con sus tipos
de datos. Aunque esto no es requerido por el lenguaje C, se prefiere en
Linux porque es una forma sencilla de añadir información valiosa para el
lector.

No utilice la palabra clave ``extern`` con declaraciones de función ya que
esto hace las líneas más largas y no es estrictamente necesario.

Al escribir prototipos de funciones, mantenga el `orden de los elementos regular
<https://lore.kernel.org/mm-commits/CAHk-=wiOCLRny5aifWNhr621kYrJwhfURsa0vFPeUEm8mF0ufg@mail.gmail.com/>`_.
Por ejemplo, usando este ejemplo de declaración de función::

 __init void * __must_check action(enum magic value, size_t size, u8 count,
				   char *fmt, ...) __printf(4, 5) __malloc;

El orden preferido de elementos para un prototipo de función es:

- clase de almacenamiento (a continuación, ``static __always_inline``,
  teniendo en cuenta que ``__always_inline`` es técnicamente un atributo
  pero se trata como ``inline``)
- atributos de clase de almacenamiento (aquí, ``__init`` -- es decir,
  declaraciones de sección, pero también cosas como ``__cold``)
- tipo de retorno (aquí, ``void *``)
- atributos de tipo de retorno (aquí, ``__must_check``)
- nombre de la función (aquí, ``action``)
- parámetros de la función (aquí, ``(enum magic value, size_t size, u8 count, char *fmt, ...)``,
  teniendo en cuenta que los nombres de los parámetros siempre deben
  incluirse)
- atributos de parámetros de función (aquí, ``__printf(4, 5)``)
- atributos de comportamiento de la función (aquí, ``__malloc``)

Tenga en cuenta que para una **definición** de función (es decir, el cuerpo
real de la función), el compilador no permite atributos de parámetros de
función después de parámetros de la función. En estos casos, deberán ir
tras los atributos de clase (por ejemplo, tenga en cuenta el cambio de
posición de ``__printf(4, 5)`` a continuación, en comparación con el
ejemplo de **declaración** anterior)::

 static __always_inline __init __printf(4, 5) void * __must_check action(enum magic value,
		size_t size, u8 count, char *fmt, ...) __malloc
 {
	...
 }

7) Salida centralizada de funciones
-----------------------------------

Aunque desaprobado por algunas personas, el equivalente de la instrucción
goto es utilizado con frecuencia por los compiladores, en forma de
instrucción de salto incondicional.

La declaración goto es útil cuando una función sale desde múltiples
ubicaciones y se deben realizar algunos trabajos comunes, como la limpieza.
Si no se necesita limpieza, entonces simplemente haga return directamente.

Elija nombres de etiquetas que digan qué hace el goto o por qué existe el
goto. Un ejemplo de un buen nombre podría ser ``out_free_buffer:``
(``salida_liberar_buffer``) si al irse libera ``buffer``. Evite usar
nombres GW-BASIC como ``err1:`` y ``err2:``, ya que tendría que volver a
numerarlos si alguna vez agrega o elimina rutas de salida, y hacen que sea
difícil de verificar que sean correctos, de todos modos.

La razón para usar gotos es:

- Las declaraciones incondicionales son más fáciles de entender y seguir.
- se reduce el anidamiento
- errores al no actualizar los puntos de salida individuales al hacer
  modificaciones son evitados
- ahorra el trabajo del compilador de optimizar código redundante ;)

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

Un tipo común de error a tener en cuenta es "un error de error" que es algo
así:

.. code-block:: c

	err:
		kfree(foo->bar);
		kfree(foo);
		return ret;

El error en este código es que en algunas rutas de salida, ``foo`` es NULL.
Normalmente la solución para esto es dividirlo en dos etiquetas de error
``err_free_bar:`` y ``err_free_foo:``:

.. code-block:: c

	 err_free_bar:
		kfree(foo->bar);
	 err_free_foo:
		kfree(foo);
		return ret;

Idealmente, debería simular errores para probar todas las rutas de salida.


8) Comentarios
--------------

Los comentarios son buenos, pero también existe el peligro de comentar
demasiado. NUNCA trate de explicar CÓMO funciona su código en un
comentario: es mucho mejor escribir el código para que el
**funcionamiento** sea obvio y es una pérdida de tiempo explicar código mal
escrito.

Generalmente, desea que sus comentarios digan QUÉ hace su código, no CÓMO.
Además, trate de evitar poner comentarios dentro del cuerpo de una función:
si la función es tan compleja que necesita comentar por separado partes de
esta, probablemente debería volver al capítulo 6 una temporada. Puede
hacer pequeños comentarios para notar o advertir sobre algo particularmente
inteligente (o feo), pero trate de evitar el exceso. En su lugar, ponga los
comentarios al principio de la función, diga a la gente lo que hace y
posiblemente POR QUÉ hace esto.

Al comentar las funciones de la API del kernel, utilice el formato
kernel-doc. Consulte los archivos en :ref:`Documentation/doc-guide/ <doc_guide>`
y ``scripts/kernel-doc`` para más detalles.

El estilo preferido para comentarios largos (de varias líneas) es:

.. code-block:: c

	/*
	* Este es el estilo preferido para comentarios
	* multilínea en el código fuente del kernel Linux.
	* Por favor, utilícelo constantemente.
	*
	* Descripción: Una columna de asteriscos en el lado izquierdo,
	* con líneas iniciales y finales casi en blanco.
	*/

Para archivos en net/ y drivers/net/, el estilo preferido para comentarios
largos (multi-linea) es un poco diferente.

.. code-block:: c

	/* El estilo de comentario preferido para archivos en net/ y drivers/net
	* se asemeja a esto.
	*
	* Es casi lo mismo que el estilo de comentario generalmente preferido,
	* pero no hay una línea inicial casi en blanco.
	*/

También es importante comentar los datos, ya sean tipos básicos o
derivados. Para este fin, use solo una declaración de datos por línea (sin
comas para múltiples declaraciones de datos). Esto le deja espacio para un
pequeño comentario sobre cada elemento, explicando su uso.

9) Has hecho un desastre
---------------------------

Está bien, todos lo hacemos. Probablemente un antiguo usuario de Unix le
haya dicho que ``GNU emacs`` formatea automáticamente las fuentes C por
usted, y ha notado que sí, lo hace, pero los por defecto que tiene son
menos que deseables (de hecho, son peores que los aleatorios) escribiendo -
un número infinito de monos escribiendo en GNU emacs nunca harán un buen
programa).

Por lo tanto, puede deshacerse de GNU emacs o cambiarlo y usar valores más
sanos. Para hacer esto último, puede pegar lo siguiente en su archivo
.emacs:

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

Esto hará que emacs funcione mejor con el estilo de código del kernel para
C en archivos bajo ``~/src/linux-trees``.

Pero incluso si no logra que emacs realice un formateo correcto, no todo
está perdido: use ``indent``.

Ahora bien, de nuevo, la sangría de GNU tiene la misma configuración de
muerte cerebral que GNU emacs tiene, por lo que necesita darle algunas
opciones de línea de comando. Sin embargo, eso no es tan malo, porque
incluso los creadores de GNU indent reconocen la autoridad de K&R (la gente
de GNU no es mala, solo están gravemente equivocados en este asunto), por
lo que simplemente de a la sangría las opciones ``-kr -i8`` (significa
``K&R, guiones de 8 caracteres``), o use ``scripts/Lindent``, que indenta
con ese estilo.

``indent`` tiene muchas opciones, y especialmente cuando se trata de
comentar reformateos, es posible que desee echar un vistazo a la página del
manual. Pero recuerde: ``indent`` no es la solución para una mala
programación.

Tenga en cuenta que también puede usar la herramienta ``clang-format`` para
ayudarlo con estas reglas, para volver a formatear rápidamente partes de su
código automáticamente, y revisar archivos completos para detectar errores
de estilo del código, errores tipográficos y posibles mejoras. También es
útil para ordenar ``#includes``, para alinear variables/macros, para
redistribuir texto y otras tareas similares. Vea el archivo
:ref:`Documentation/process/clang-format.rst <clangformat>` para más
detalles.

10) Archivos de configuración de Kconfig
----------------------------------------

Para todos los archivos de configuración de Kconfig* en todo el árbol
fuente, la sangría es algo diferente. Las líneas bajo una definición
``config`` están indentadas con una tabulación, mientras que el texto de
ayuda tiene una sangría adicional de dos espacios. Ejemplo::

  config AUDIT
	bool "Soporte para auditar"
	depends on NET
	help
	  Habilita la infraestructura de auditoría que se puede usar con otro
	  subsistema kernel, como SELinux (que requiere esto para
	  registro de salida de mensajes avc). No hace auditoría de llamadas al
    sistema sin CONFIG_AUDITSYSCALL.

Características seriamente peligrosas (como soporte de escritura para
ciertos filesystems) deben anunciar esto de forma destacada en su cadena de
solicitud::

  config ADFS_FS_RW
	bool "ADFS write support (DANGEROUS)"
	depends on ADFS_FS
	...

Para obtener la documentación completa sobre los archivos de configuración,
consulte el archivo Documentation/kbuild/kconfig-language.rst.


11) Estructuras de datos
------------------------

Las estructuras de datos que tienen visibilidad fuera del contexto de un
solo subproceso en el que son creadas y destruidas, siempre debe tener
contadores de referencia. En el kernel, la recolección de basura no existe
(y fuera, la recolección de basura del kernel es lenta e ineficiente), lo
que significa que absolutamente **tiene** para hacer referencia y contar
todos sus usos.

El conteo de referencias significa que puede evitar el bloqueo y permite
que múltiples usuarios tengan acceso a la estructura de datos en paralelo -
y no tengan que preocuparse de que la estructura, de repente, desaparezca
debajo de su control, solo porque durmieron o hicieron otra cosa por un
tiempo.

Tenga en cuenta que el bloqueo **no** reemplaza el recuento de referencia.
El bloqueo se utiliza para mantener la coherencia de las estructuras de
datos, mientras que la referencia y contar es una técnica de gestión de
memoria. Por lo general, ambos son necesarios, y no deben confundirse entre
sí.

De hecho, muchas estructuras de datos pueden tener dos niveles de conteo de
referencias, cuando hay usuarios de diferentes ``clases``. El conteo de
subclases cuenta el número de usuarios de la subclase y disminuye el conteo
global solo una vez, cuando el recuento de subclases llega a cero.

Se pueden encontrar ejemplos de este tipo de ``recuento de referencias de
niveles múltiples`` en la gestión de memoria (``struct mm_struct``:
mm_users y mm_count), y en código del sistema de archivos
(``struct super_block``: s_count y s_active).

Recuerde: si otro hilo puede encontrar su estructura de datos y usted no
tiene un recuento de referencias, es casi seguro que tiene un error.

12) Macros, Enums y RTL
------------------------

Los nombres de macros que definen constantes y etiquetas en enumeraciones
(enums) están en mayúsculas.

.. code-block:: c

	#define CONSTANTE 0x12345

Se prefieren los enums cuando se definen varias constantes relacionadas.

Se aprecian los nombres de macro en MAYÚSCULAS, pero las macros que se
asemejan a funciones puede ser nombradas en minúscula.

Generalmente, las funciones en línea son preferibles a las macros que se
asemejan a funciones.

Las macros con varias instrucciones deben contenerse en un bloque do-while:

.. code-block:: c

	#define macrofun(a, b, c)			\
		do {					\
			if (a == 5)			\
				haz_esto(b, c);		\
		} while (0)

Cosas a evitar al usar macros:

1) macros que afectan el flujo de control:

.. code-block:: c

	#define FOO(x)					\
		do {					\
			if (blah(x) < 0)		\
				return -EBUGGERED;	\
		} while (0)

es una **muy** mala idea. Parece una llamada de función pero sale de la
función de ``llamada``; no rompa los analizadores internos de aquellos que
leerán el código.

2) macros que dependen de tener una variable local con un nombre mágico:

.. code-block:: c

	#define FOO(val) bar(index, val)

puede parecer algo bueno, pero es confuso como el infierno cuando uno lee
el código, y es propenso a romperse por cambios aparentemente inocentes.

3) macros con argumentos que se usan como valores l: FOO(x) = y; le van
a morder si alguien, por ejemplo, convierte FOO en una función en línea.

4) olvidarse de la precedencia: las macros que definen constantes usando
expresiones deben encerrar la expresión entre paréntesis. Tenga cuidado con
problemas similares con macros usando parámetros.

.. code-block:: c

	#define CONSTANTE 0x4000
	#define CONSTEXP (CONSTANTE | 3)

5) colisiones de espacio de nombres ("namespace") al definir variables
locales en macros que se asemejan a funciones:

.. code-block:: c

	#define FOO(x)				\
	({					\
		typeof(x) ret;			\
		ret = calc_ret(x);		\
		(ret);				\
	})

ret es un nombre común para una variable local -es menos probable que
__foo_ret colisione (coincida) con una variable existente.

El manual de cpp trata las macros de forma exhaustiva. El manual interno de
gcc también cubre RTL, que se usa frecuentemente con lenguaje ensamblador
en el kernel.

13) Imprimir mensajes del kernel
--------------------------------

A los desarrolladores del kernel les gusta ser vistos como alfabetizados.
Cuide la ortografía de los mensajes del kernel para causar una buena
impresión. No utilice contracciones incorrectas como ``dont``; use
``do not`` o ``don't`` en su lugar. Haga sus mensajes concisos, claros e
inequívocos.

Los mensajes del kernel no tienen que terminar con un punto.

Imprimir números entre paréntesis (%d) no agrega valor y debe evitarse.

Hay varias modelos de macros de diagnóstico de driver en <linux/dev_printk.h>
que debe usar para asegurarse de que los mensajes coincidan con el
dispositivo correcto y driver, y están etiquetados con el nivel correcto:
dev_err(), dev_warn(), dev_info(), y así sucesivamente. Para mensajes que
no están asociados con un dispositivo particular, <linux/printk.h> define
pr_notice(), pr_info(), pr_warn(), pr_err(), etc.

Crear buenos mensajes de depuración puede ser todo un desafío; y una vez
los tiene, pueden ser de gran ayuda para la resolución remota de problemas.
Sin embargo, la impresión de mensajes de depuración se maneja de manera
diferente a la impresión de otros mensajes que no son de depuración.
Mientras que las otras funciones pr_XXX() se imprimen incondicionalmente,
pr_debug() no lo hace; se compila fuera por defecto, a menos que DEBUG sea
definido o se establezca CONFIG_DYNAMIC_DEBUG. Eso es cierto para dev_dbg()
también, y una convención relacionada usa VERBOSE_DEBUG para agregar
mensajes dev_vdbg() a los ya habilitados por DEBUG.

Muchos subsistemas tienen opciones de depuración de Kconfig para activar
-DDEBUG en el Makefile correspondiente; en otros casos, los archivos
usan #define DEBUG. Y cuando un mensaje de depuración debe imprimirse
incondicionalmente, por ejemplo si es ya dentro de una sección #ifdef
relacionada con la depuración, printk(KERN_DEBUG ...) puede ser usado.

14) Reservando memoria
----------------------

El kernel proporciona los siguientes asignadores de memoria de propósito
general: kmalloc(), kzalloc(), kmalloc_array(), kcalloc(), vmalloc() y
vzalloc(). Consulte la documentación de la API para obtener más información.
a cerca de ellos. :ref:`Documentation/core-api/memory-allocation.rst
<memory_allocation>`

La forma preferida para pasar el tamaño de una estructura es la siguiente:

.. code-block:: c

	p = kmalloc(sizeof(*p), ...);

La forma alternativa donde se deletrea el nombre de la estructura perjudica
la legibilidad, y presenta una oportunidad para un error cuando se cambia
el tipo de variable de puntero, pero el tamaño correspondiente de eso que
se pasa a un asignador de memoria no.

Convertir el valor devuelto, que es un puntero vacío, es redundante. La
conversión desde el puntero vacío a cualquier otro tipo de puntero está
garantizado por la programación en idioma C.

La forma preferida para asignar una matriz es la siguiente:

.. code-block:: c

	p = kmalloc_array(n, sizeof(...), ...);

La forma preferida para asignar una matriz a cero es la siguiente:

.. code-block:: c

	p = kcalloc(n, sizeof(...), ...);

Ambos casos verifican el desbordamiento en el tamaño de asignación n *
sizeof (...), y devuelven NULL si esto ocurrió.

Todas estas funciones de asignación genéricas emiten un volcado de pila
(" stack dump") en caso de fallo cuando se usan sin __GFP_NOWARN, por lo
que no sirve de nada emitir un mensaje de fallo adicional cuando se
devuelva NULL.

15) La enfermedad de inline
----------------------------

Parece haber una común percepción errónea de que gcc tiene una magica
opción "hazme más rápido" de aceleración, llamada ``inline`` (en línea).
Mientras que el uso de inlines puede ser apropiado (por ejemplo, como un
medio para reemplazar macros, consulte el Capítulo 12), muy a menudo no lo
es. El uso abundante de la palabra clave inline conduce a una mayor kernel,
que a su vez ralentiza el sistema en su conjunto, debido a una mayor huella
de icache para la CPU, y sencillamente porque hay menos memoria disponible
para el pagecache. Solo piense en esto; un fallo en la memoria caché de la
página provoca una búsqueda de disco, que tarda fácilmente 5 milisegundos.
Hay MUCHOS ciclos de CPU que puede entrar en estos 5 milisegundos.

Una razonable regla general es no poner funciones inline que tengan más de
3 líneas de código en ellas. Una excepción a esta regla son los casos en
que se sabe que un parámetro es una constante en tiempo de compilación, y
como resultado de esto, usted *sabe*, el compilador podrá optimizar la
mayor parte de su función en tiempo de compilación. Para un buen ejemplo de
este último caso, véase la función en línea kmalloc().

A menudo, la gente argumenta que agregar funciones en línea que son
estáticas y se usan solo una vez, es siempre una victoria ya que no hay
perdida de espacio. Mientras esto es técnicamente correcto, gcc es capaz de
incorporarlos automáticamente sin ayuda, y esta el problema de
mantenimiento de eliminar el inline, cuando un segundo usuario supera el
valor potencial de la pista que le dice a gcc que haga algo que habría
hecho de todos modos.

16) Valores devueltos por función y sus nombres
-----------------------------------------------

Las funciones pueden devolver valores de muchos tipos diferentes, y uno de
lo más común es un valor que indica si la función tuvo éxito o ha fallado.
Dicho valor se puede representar como un número entero de código de error
(-Exxx = falla, 0 = éxito) o un booleano ``con éxito`` (0 = falla, distinto
de cero = éxito).

La mezcla de estos dos tipos de representaciones es una fuente fértil de
errores difíciles de encontrar. Si el lenguaje C incluyera una fuerte
distinción entre enteros y booleanos, el compilador encontraría estos
errores por nosotros... pero no lo hace. Para ayudar a prevenir tales
errores, siga siempre esta convención::

	Si el nombre de una función es una acción o un comando imperativo,
	la función debe devolver un número entero de código de error. si el nombre
	es un predicado, la función debe devolver un valor booleano "exitoso".

Por ejemplo, ``agregar trabajo`` es un comando, y la función
agregar_trabajo() devuelve 0 en caso de éxito o -EBUSY en caso de fracaso.
De la misma manera, ``dispositivo PCI presente`` es un predicado, y la
función pci_dev_present() devuelve 1 si tiene éxito en encontrar un
dispositivo coincidente o 0 si no es así.

Todas las funciones EXPORTed (exportadas) deben respetar esta convención,
al igual que todas las funciones publicas. Las funciones privadas
(estáticas) no lo necesitan, pero es recomendado que lo hagan.

Las funciones cuyo valor devuelto es el resultado real de un cálculo, en
lugar de una indicación de si el cómputo tuvo éxito, no están sujetas a
esta regla. Generalmente indican fallo al devolver valores fuera del rango
de resultados. Los ejemplos típicos serían funciones que devuelven
punteros; estos usan NULL o el mecanismo ERR_PTR para informar de fallos.

17) Usando bool
----------------

El tipo bool del kernel Linux es un alias para el tipo C99 _Bool. Los
valores booleanos pueden solo evaluar a 0 o 1, y la conversión implícita o
explícita a bool convierte automáticamente el valor en verdadero o falso.
Cuando se utilizan tipos booleanos,
!! no se necesita construcción, lo que elimina una clase de errores.

Cuando se trabaja con valores booleanos, se deben usar las definiciones
verdadera y falsa, en lugar de 1 y 0.

Los tipos de devolución de función bool y las variables de pila siempre
se pueden usar cuando esto sea adecuado. Se recomienda el uso de bool para
mejorar la legibilidad y, a menudo, es una mejor opción que 'int' para
almacenar valores booleanos.

No use bool si el diseño de la línea de caché o el tamaño del valor son
importantes, ya que su tamaño y la alineación varía según la arquitectura
compilada. Las estructuras que son optimizadas para la alineación y el
tamaño no debe usar bool.

Si una estructura tiene muchos valores verdadero/falso, considere
consolidarlos en un bitfield con miembros de 1 bit, o usando un tipo de
ancho fijo apropiado, como u8.

De manera similar, para los argumentos de función, se pueden consolidar
muchos valores verdaderos/falsos en un solo argumento bit a bit 'flags' y
'flags' a menudo, puede ser una alternativa de argumento más legible si los
sitios de llamada tienen constantes desnudas de tipo verdaderas/falsas.

De lo contrario, el uso limitado de bool en estructuras y argumentos puede
mejorar la legibilidad.

18) No reinvente las macros del kernel
---------------------------------------

El archivo de cabecera include/linux/kernel.h contiene una serie de macros
que debe usar, en lugar de programar explícitamente alguna variante de
estos por usted mismo. Por ejemplo, si necesita calcular la longitud de una
matriz, aproveche la macro

.. code-block:: c

	#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

De manera similar, si necesita calcular el tamaño de algún miembro de la
estructura, use

.. code-block:: c

	#define sizeof_field(t, f) (sizeof(((t*)0)->f))

También hay macros min() y max() que realizan una verificación estricta de
tipos si lo necesita. Siéntase libre de leer detenidamente ese archivo de
encabezado para ver qué más ya está definido y que no debe reproducir en su
código.

19) Editores modeline y otros desastres
---------------------------------------

Algunos editores pueden interpretar la información de configuración
incrustada en los archivos fuente, indicado con marcadores especiales. Por
ejemplo, emacs interpreta las líneas marcadas como esto:

.. code-block:: c

	-*- mode: c -*-

O así:

.. code-block:: c

	/*
	Local Variables:
	compile-command: "gcc -DMAGIC_DEBUG_FLAG foo.c"
	End:
	*/

Vim interpreta los marcadores que se ven así:

.. code-block:: c

	/* vim:set sw=8 noet */

No incluya ninguno de estos en los archivos fuente. La gente tiene sus
propias configuraciones del editor, y sus archivos de origen no deben
anularlos. Esto incluye marcadores para sangría y configuración de modo.
La gente puede usar su propio modo personalizado, o puede tener algún otro
método mágico para que la sangría funcione correctamente.


20) Ensamblador inline
-----------------------

En el código específico de arquitectura, es posible que deba usar
ensamblador en línea para interactuar con funcionalidades de CPU o
plataforma. No dude en hacerlo cuando sea necesario. Sin embargo, no use
ensamblador en línea de forma gratuita cuando C puede hacer el trabajo.
Puede y debe empujar el hardware desde C cuando sea posible.

Considere escribir funciones auxiliares simples que envuelvan bits comunes
de ensamblador, en lugar de escribirlos repetidamente con ligeras
variaciones. Recuerde que el ensamblador en línea puede usar parámetros C.

Las funciones de ensamblador grandes y no triviales deben ir en archivos .S,
con su correspondientes prototipos de C definidos en archivos de encabezado
en C. Los prototipos de C para el ensamblador deben usar ``asmlinkage``.

Es posible que deba marcar su declaración asm como volátil, para evitar que
GCC la elimine si GCC no nota ningún efecto secundario. No siempre es
necesario hacerlo, sin embargo, y hacerlo innecesariamente puede limitar la
optimización.

Al escribir una sola declaración de ensamblador en línea que contiene
múltiples instrucciones, ponga cada instrucción en una línea separada en
una string separada, y termine cada string excepto la última con ``\n\t``
para indentar correctamente la siguiente instrucción en la salida en
ensamblador:

.. code-block:: c

	asm ("magic %reg1, #42\n\t"
	     "more_magic %reg2, %reg3"
	     : /* outputs */ : /* inputs */ : /* clobbers */);

21) Compilación condicional
---------------------------

Siempre que sea posible, no use condicionales de preprocesador (#if,
#ifdef) en archivos .c; de lo contrario, el código es más difícil de leer y
la lógica más difícil de seguir. En cambio, use dichos condicionales en un
archivo de encabezado que defina funciones para usar en esos archivos .c,
proporcionando versiones de código auxiliar sin operación en el caso #else,
y luego llame a estas funciones incondicionalmente desde archivos .c. El
compilador evitará generar cualquier código para las llamadas restantes,
produciendo resultados idénticos, pero la lógica es fácil de seguir.

Prefiera compilar funciones completas, en lugar de porciones de funciones o
porciones de expresiones. En lugar de poner un ifdef en una expresión,
divida la totalidad de la expresión con una función de ayuda independiente
y aplique el condicional a esa función.

Si tiene una función o variable que puede potencialmente quedar sin usar en
una configuración en particular, y el compilador advertiría sobre su
definición sin usar, marque la definición como __maybe_unused en lugar de
envolverla en un preprocesador condicional. (Sin embargo, si una función o
variable *siempre* acaba sin ser usada, bórrela.)

Dentro del código, cuando sea posible, use la macro IS_ENABLED para
convertir un símbolo Kconfig en una expresión booleana de C, y utilícelo en
un condicional de C normal:

.. code-block:: c

	if (IS_ENABLED(CONFIG_SOMETHING)) {
		...
	}

El compilador "doblará"" constantemente el condicional e incluirá o
excluirá el bloque de código al igual que con un #ifdef, por lo que esto no
agregará ningún tiempo de gastos generales en ejecución. Sin embargo, este
enfoque todavía permite que el compilador de C vea el código dentro del
bloque, y verifique que sea correcto (sintaxis, tipos, símbolo, referencias,
etc.). Por lo tanto, aún debe usar un #ifdef si el código dentro del bloque
hace referencia a símbolos que no existirán si no se cumple la condición.

Al final de cualquier bloque #if o #ifdef no trivial (más de unas pocas
líneas), incluya un comentario después de #endif en la misma línea,
anotando la expresión condicional utilizada. Por ejemplo:

.. code-block:: c

	#ifdef CONFIG_SOMETHING
	...
	#endif /* CONFIG_SOMETHING */

22) No rompa el kernel
-----------------------

En general, la decisión de romper el kernel pertenece al usuario, más que
al desarrollador del kernel.

Evite el panic()
****************

panic() debe usarse con cuidado y principalmente solo durante el arranque
del sistema. panic() es, por ejemplo, aceptable cuando se queda sin memoria
durante el arranque y no puede continuar.

Use WARN() en lugar de BUG()
****************************

No agregue código nuevo que use cualquiera de las variantes BUG(), como
BUG(), BUG_ON() o VM_BUG_ON(). En su lugar, use una variante WARN*(),
preferiblemente WARN_ON_ONCE(), y posiblemente con código de recuperación.
El código de recuperación no es requerido si no hay una forma razonable de
recuperar, al menos parcialmente.

"Soy demasiado perezoso para tener en cuenta los errores" no es una excusa
para usar BUG(). Importantes corrupciones internas sin forma de continuar
aún pueden usar BUG(), pero necesitan una buena justificación.

Use WARN_ON_ONCE() en lugar de WARN() o WARN_ON()
*************************************************

Generalmente, se prefiere WARN_ON_ONCE() a WARN() o WARN_ON(), porque es
común que una condición de advertencia dada, si ocurre, ocurra varias
veces. Esto puede llenar el registro del kernel, e incluso puede ralentizar
el sistema lo suficiente como para que el registro excesivo se convierta en
su propio, adicional problema.

No haga WARN a la ligera
************************

WARN*() está diseñado para situaciones inesperadas que nunca deberían
suceder. Las macros WARN*() no deben usarse para nada que se espera que
suceda durante un funcionamiento normal. No hay "checkeos" previos o
posteriores a la condición, por ejemplo. De nuevo: WARN*() no debe usarse
para una condición esperada que vaya a activarse fácilmente, por ejemplo,
mediante acciones en el espacio del usuario. pr_warn_once() es una
alternativa posible, si necesita notificar al usuario de un problema.

No se preocupe sobre panic_on_warn de usuarios
**********************************************

Algunas palabras más sobre panic_on_warn: Recuerde que ``panic_on_warn`` es
una opción disponible del kernel, y que muchos usuarios configuran esta
opción. Esta es la razón por la que hay un artículo de "No haga WARN a la
ligera", arriba. Sin embargo, la existencia de panic_on_warn de usuarios no
es una razón válida para evitar el uso juicioso de WARN*(). Esto se debe a
que quien habilita panic_on_warn, explícitamente pidió al kernel que
fallara si se dispara un WARN*(), y tales usuarios deben estar preparados
para afrontar las consecuencias de un sistema que es algo más probable que
se rompa.

Use BUILD_BUG_ON() para aserciones en tiempo de compilación
***********************************************************

El uso de BUILD_BUG_ON() es aceptable y recomendado, porque es una aserción
en tiempo de compilación, que no tiene efecto en tiempo de ejecución.

Apéndice I) Referencias
-----------------------

The C Programming Language, Segunda edicion
por Brian W. Kernighan and Dennis M. Ritchie.
Prentice Hall, Inc., 1988.
ISBN 0-13-110362-8 (paperback), 0-13-110370-9 (hardback).

The Practice of Programming
por Brian W. Kernighan and Rob Pike.
Addison-Wesley, Inc., 1999.
ISBN 0-201-61586-X.

manuales GCC - en cumplimiento con K&R y este texto - para cpp, gcc,
detalles de gcc y sangría, todo disponible en https://www.gnu.org/manual/

WG14 es el grupo de trabajo de estandarización internacional de la
programación en lenguaje C, URL: http://www.open-std.org/JTC1/SC22/WG14/

:ref:`process/coding-style.rst <codingstyle>` del kernel, por greg@kroah.com at OLS 2002:
http://www.kroah.com/linux/talks/ols_2002_kernel_codingstyle_talk/html/
