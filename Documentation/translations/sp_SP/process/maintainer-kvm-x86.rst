.. include:: ../disclaimer-sp.rst

:Original: Documentation/process/maintainer-kvm-x86.rst
:Translator: Juan Embid <jembid@ucm.es>

KVM x86
=======

Prólogo
--------
KVM se esfuerza por ser una comunidad acogedora; las contribuciones de los
recién llegados son valoradas e incentivadas. Por favor, no se desanime ni
se sienta intimidado por la extensión de este documento y las numerosas
normas/directrices que contiene. Todos cometemos errores y todos hemos sido
principiantes en algún momento. Mientras haga un esfuerzo honesto por
seguir las directrices de KVM x86, sea receptivo a los comentarios, y
aprenda de los errores que cometa, será recibido con los brazos abiertos,
no con antorchas y horcas.

TL;DR
-----
Las pruebas son obligatorias. Sea coherente con los estilos y patrones
establecidos.

Árboles
-------
KVM x86 se encuentra actualmente en un período de transición de ser parte
del árbol principal de KVM, a ser "sólo otra rama de KVM". Como tal, KVM
x86 está dividido entre el árbol principal de KVM,
``git.kernel.org/pub/scm/virt/kvm/kvm.git``, y un árbol específico de KVM
x86, ``github.com/kvm-x86/linux.git``.

Por lo general, las correcciones para el ciclo en curso se aplican
directamente al árbol principal de KVM, mientras que todo el desarrollo
para el siguiente ciclo se dirige a través del árbol de KVM x86. En el
improbable caso de que una corrección para el ciclo actual se dirija a
través del árbol KVM x86, se aplicará a la rama ``fixes`` antes de llegar
al árbol KVM principal.

Tenga en cuenta que se espera que este periodo de transición dure bastante
tiempo, es decir, que será el statu quo en un futuro previsible.

Ramas
~~~~~
El árbol de KVM x86 está organizado en múltiples ramas por temas. El
propósito de utilizar ramas temáticas más específicas es facilitar el
control de un área de desarrollo, y para limitar los daños colaterales de
errores humanos y/o commits con errores, por ejemplo, borrar el commit HEAD
de una rama temática no tiene impacto en los hashes SHA1 de otros commit
en en camino, y tener que rechazar una solicitud de pull debido a errores
retrasa sólo esa rama temática.

Todas las ramas temáticas, excepto ``next`` y ``fixes``, se agrupan en
``next`` a través de un Cthulhu merge en función de las necesidades, es
decir, cuando se actualiza una rama temática. Como resultado, los push
forzados a ``next`` son comunes.

Ciclo de Vida
~~~~~~~~~~~~~
Las correcciones dirigidas a la versión actual, también conocida como
mainline, suelen aplicarse directamente al árbol principal de KVM, es
decir, no pasan por el árbol x86 de KVM.

Los cambios dirigidos a la siguiente versión se dirigen a través del árbol
KVM x86. Se envían pull requests (de KVM x86 a KVM main) para cada rama
temática de KVM x86, normalmente la semana antes de que Linus abra la
ventana de fusión, por ejemplo, la semana siguiente a rc7 para las
versiones "normales". Si todo va bien, las ramas temáticas son subidas en
el pull request principal de KVM enviado durante la ventana de fusión de
Linus.

El árbol de KVM x86 no tiene su propia ventana de fusión oficial, pero hay
un cierre suave alrededor de rc5 para nuevas características, y un cierre
suave alrededor de rc6 para correcciones (para la próxima versión; fíjese
más arriba para las correcciones dirigidas a la versión actual).

Cronología
~~~~~~~~~~
Normalmente, los envíos se revisan y aplican en orden FIFO, con cierto
margen de maniobra en función del tamaño de la serie, los parches que están
"calientes en caché", etc. Correcciones, especialmente para la versión
actual y/o árboles estables, consiguen saltar la cola. Los parches que se
lleven a través de un árbol que no sea KVM (la mayoría de las veces a
través del árbol de consejos) y/o que tengan otros acks/revisiones también
saltan la cola hasta cierto punto.

Tenga en cuenta que la mayor parte de la revisión se realiza entre rc1 y
rc6, más o menos. El periodo entre la rc6 y la siguiente rc1 se utiliza
para ponerse al día en otras tareas, es decir, la falta de envíos durante
este periodo no es inusual.

Los pings para obtener una actualización del estado son bienvenidos, pero
tenga en cuenta el calendario del ciclo de publicación actual y tenga
expectativas realistas. Si está haciendo ping para la aceptación, es decir,
no sólo para obtener comentarios o una actualización, por favor haga todo
lo posible, dentro de lo razonable, para asegurarse de que sus parches
están listos para ser fusionados. Los pings sobre series que rompen la
compilación o fallan en las pruebas provocan el descontento de los
mantenedores.

Desarrollo
-----------

Árbol base/Rama
~~~~~~~~~~~~~~~
Las correcciones dirigidas a la versión actual, también conocida como
mainline, deben basarse en
``git://git.kernel.org/pub/scm/virt/kvm/kvm.git master``. Tenga en cuenta
que las correcciones no garantizan automáticamente la inclusión en la
versión actual. No hay una regla única, pero normalmente sólo las
correcciones de errores urgentes, críticos y/o introducidos en la versión
actual deberían incluirse en la versión actual.

Todo lo demás debería basarse en ``kvm-x86/next``, es decir, no hay
necesidad de seleccionar una rama temática específica como base. Si hay
conflictos y/o dependencias entre ramas, es trabajo del mantenedor
resolverlos.

La única excepción al uso de ``kvm-x86/next`` como base es si un
parche/serie es una serie multi-arquitectura, es decir, tiene
modificaciones no triviales en el código común de KVM y/o tiene cambios más
que superficiales en el código de otras arquitecturas. Los parches/series
multi-arquitectura deberían basarse en un punto común y estable en la
historia de KVM, por ejemplo, la versión candidata en la que se basa
``kvm-x86 next``. Si no está seguro de si un parche/serie es realmente
multiarquitectura, sea precavido y trátelo como multiarquitectura, es
decir, utilice una base común.

Estilo del codigo
~~~~~~~~~~~~~~~~~~~~~~
Cuando se trata de estilo, nomenclatura, patrones, etc., la coherencia es
la prioridad número uno en KVM x86. Si todo lo demás falla, haga coincidir
lo que ya existe.

Con algunas advertencias que se enumeran a continuación, siga las
recomendaciones de los responsables del árbol de consejos
:ref:`maintainer-tip-coding-style`, ya que los parches/series a menudo
tocan tanto archivos x86 KVM como no KVM, es decir, llaman la atención de
los mantenedores de KVM *y* del árbol de consejos.

El uso del abeto inverso, también conocido como árbol de Navidad inverso o
árbol XMAS inverso, para las declaraciones de variables no es estrictamente
necesario, aunque es preferible.

Excepto para unos pocos apuntes especiales, no utilice comentarios
kernel-doc para las funciones. La gran mayoría de las funciones "públicas"
de KVM no son realmente públicas, ya que están destinadas únicamente al
consumo interno de KVM (hay planes para privatizar las cabeceras y
exportaciones de KVM para reforzar esto).

Comentarios
~~~~~~~~~~~
Escriba los comentarios en modo imperativo y evite los pronombres. Utilice
los comentarios para ofrecer una visión general de alto nivel del código
y/o para explicar por qué el código hace lo que hace. No reitere lo que el
código hace literalmente; deje que el código hable por sí mismo. Si el
propio código es inescrutable, los comentarios no servirán de nada.

Referencias SDM y APM
~~~~~~~~~~~~~~~~~~~~~~
Gran parte de la base de código de KVM está directamente vinculada al
comportamiento de la arquitectura definido en El Manual de Desarrollo de
Software (SDM) de Intel y el Manual del Programador de Arquitectura (APM)
de AMD. El uso de "SDM de Intel" y "APM de AMD", o incluso sólo "SDM" o
"APM", sin contexto adicional es correcto.

No haga referencia a secciones específicas, tablas, figuras, etc. por su
número, especialmente en los comentarios. En su lugar, si es necesario
(véase más abajo), copie y pegue el fragmento correspondiente y haga
referencia a las secciones/tablas/figuras por su nombre. Los diseños del
SDM y el APM cambian constantemente, por lo que los números/etiquetas no
son estables.

En general, no haga referencia explícita ni copie-pegue del SDM o APM en
los comentarios. Con pocas excepciones, KVM *debe* respetar el
comportamiento de la arquitectura, por lo que está implícito que el
comportamiento de KVM está emulando el comportamiento de SDM y/o APM. Tenga
en cuenta que hacer referencia al SDM/APM en los registros de cambios para
justificar el cambio y proporcionar contexto es perfectamente correcto y
recomendable.

Shortlog
~~~~~~~~
El formato de prefijo más recomendable es ``KVM: <topic>:``, donde
``<topic>`` es uno de los siguientes::

- x86
- x86/mmu
- x86/pmu
- x86/xen
- autocomprobaciones
- SVM
- nSVM
- VMX
- nVMX

**¡NO use x86/kvm!** ``x86/kvm`` se usa exclusivamente para cambios de
Linux virtualizado por KVM, es decir, para arch/x86/kernel/kvm.c. No use
nombres de archivos o archivos completos como prefijo de asunto/shortlog.

Tenga en cuenta que esto no coincide con las ramas temáticas (las ramas
temáticas se preocupan mucho más por los conflictos de código).

Todos los nombres distinguen entre mayúsculas y minúsculas. ``KVM: x86:``
es correcto, ``kvm: vmx:`` no lo es.

Escriba en mayúsculas la primera palabra de la descripción condensada del
parche, pero omita la puntuación final. Por ejemplo::

	KVM: x86: Corregir una desviación de puntero nulo en function_xyz()

no::

	kvm: x86: corregir una desviación de puntero nulo en function_xyz.

Si un parche afecta a varios temas, recorra el árbol conceptual hasta
encontrar el primer padre común (que suele ser simplemente ``x86``). En
caso de duda, ``git log path/to/file`` debería proporcionar una pista
razonable.

De vez en cuando surgen nuevos temas, pero le rogamos que inicie un debate
en la lista si desea proponer la introducción de un nuevo tema, es decir,
no se ande con rodeos.

Consulte :ref:`the_canonical_patch_format` para obtener más información,
con una enmienda: no trate el límite de 70-75 caracteres como un límite
absoluto y duro. En su lugar, utilice 75 caracteres como límite firme, pero
no duro, y 80 caracteres como límite duro. Es decir, deje que el registro
corto sobrepase en algunos caracteres el límite estándar si tiene una buena
razón para hacerlo.

Registro de cambios
~~~~~~~~~~~~~~~~~~~
Y lo que es más importante, escriba los registros de cambios en modo
imperativo y evite los pronombres.

Consulte :ref:`describe_changes` para obtener más información, con una
recomendación: comience con un breve resumen de los cambios reales y
continúe con el contexto y los antecedentes. Nota. Este orden entra en
conflicto directo con el enfoque preferido del árbol de sugerencias. Por
favor, siga el estilo preferido del árbol de sugerencias cuando envíe
parches. que se dirigen principalmente a código arch/x86 que _NO_ es código
KVM.

KVM x86 prefiere indicar lo que hace un parche antes de entrar en detalles
por varias razones. En primer lugar, el código que realmente se está
cambiando es posiblemente la información más importante, por lo que esa
información debe ser fácil de encontrar. Changelogs que entierran el "qué
está cambiando realmente" en una sola línea después de 3+ párrafos de fondo
hacen muy difícil encontrar esa información.

Para la revisión inicial, se podría argumentar que "lo que está roto" es
más importante, pero para hojear los registros y la arqueología git, los
detalles escabrosos importan cada vez menos. Por ejemplo, al hacer una
serie de "git blame", los detalles de cada cambio a lo largo del camino son
inútiles, los detalles sólo importan para el culpable. Proporcionar el "qué
ha cambiado" facilita determinar rápidamente si una confirmación puede ser
de interés o no.

Otra ventaja de decir primero "qué cambia" es que casi siempre es posible
decir "qué cambia" en una sola frase. A la inversa, todo menos los errores
más simples requieren varias frases o párrafos para describir el problema.
Si tanto "qué está cambiando" como "cuál es el fallo" son muy breves, el
orden no importa. Pero si uno es más corto (casi siempre el "qué está
cambiando"), entonces cubrir el más corto primero es ventajoso porque es
menos inconveniente para los lectores/revisores que tienen una preferencia
estricta de orden. Por ejemplo, tener que saltarse una frase para llegar al
contexto es menos doloroso que tener que saltarse tres párrafos para llegar
a "lo que cambia".

Arreglos
~~~~~~~~
Si un cambio corrige un error de KVM/kernel, añada una etiqueta Fixes:
incluso si el cambio no necesita ser retroportado a kernels estables, e
incluso si el cambio corrige un error en una versión anterior.

Por el contrario, si es necesario hacer una corrección, etiquete
explícitamente el parche con "Cc: stable@vger.kernel" (aunque no es
necesario que el correo electrónico incluya Cc: stable); KVM x86 opta por
excluirse del backporting Correcciones: por defecto. Algunos parches
seleccionados automáticamente se retroportan, pero requieren la aprobación
explícita de los mantenedores (busque MANUALSEL).

Referencias a Funciones
~~~~~~~~~~~~~~~~~~~~~~~
Cuando se mencione una función en un comentario, registro de cambios o
registro abreviado (o en cualquier otro lugar), utilice el formato
``nombre_de_la_función()``. Los paréntesis proporcionan contexto y
desambiguan la referencia.

Pruebas
~~~~~~~
Como mínimo, *todos* los parches de una serie deben construirse limpiamente
para KVM_INTEL=m KVM_AMD=m, y KVM_WERROR=y. Construir todas las
combinaciones posibles de Kconfigs no es factible, pero cuantas más mejor.
KVM_SMM, KVM_XEN, PROVE_LOCKING, y X86_64 son particularmente interesantes.

También es obligatorio ejecutar las autopruebas y las pruebas unitarias de
KVM (y, como es obvio, las pruebas deben pasar). La única excepción es para
los cambios que tienen una probabilidad insignificante de afectar al
comportamiento en tiempo de ejecución, por ejemplo, parches que sólo
modificar los comentarios. Siempre que sea posible y pertinente, se
recomienda encarecidamente realizar pruebas tanto en Intel como en AMD. Se
recomienda arrancar una máquina virtual real, pero no es obligatorio.

Para cambios que afecten al código de paginación en la sombra de KVM, es
obligatorio ejecutar con TDP (EPT/NPT) deshabilitado. Para cambios que
afecten al código MMU común de KVM, se recomienda encarecidamente ejecutar
con TDP deshabilitado. Para todos los demás cambios, si el código que se
está modificando depende de y/o interactúa con un parámetro del módulo, es
obligatorio realizar pruebas con la configuración correspondiente.

Tenga en cuenta que las autopruebas de KVM y las pruebas de unidad de KVM
tienen fallos conocidos. Si sospecha que un fallo no se debe a sus cambios,
verifique que el *exactamente el mismo* fallo se produce con y sin sus
cambios.

Los cambios que afecten a la documentación de texto reestructurado, es
decir, a los archivos .rst, deben generar htmldocs de forma limpia, es
decir, sin advertencias ni errores.

Si no puede probar completamente un cambio, por ejemplo, por falta de
hardware, indique claramente qué nivel de pruebas ha podido realizar, por
ejemplo, en la carta de presentación.

Novedades
~~~~~~~~~
Con una excepción, las nuevas características *deben* venir con cobertura
de pruebas. Las pruebas específicas de KVM no son estrictamente necesarias,
por ejemplo, si la cobertura se proporciona mediante la ejecución de una
prueba de VM huésped suficientemente habilitada, o ejecutando una
autoprueba de kernel relacionada en una VM, pero en todos los casos se
prefieren las pruebas KVM dedicadas. Los casos de prueba negativos en
particular son obligatorios para la habilitación de nuevas características
de hardware, ya que los flujos de errores y excepciones rara vez se
ejercitan simplemente ejecutando una VM.

La única excepción a esta regla es si KVM está simplemente anunciando
soporte para un a través de KVM_GET_SUPPORTED_CPUID, es decir, para
instrucciones/funciones que KVM no puede impedir que utilice una VM y
para las que no existe una verdadera habilitación.

Tenga en cuenta que "nuevas características" no significa sólo "nuevas
características de hardware". Las nuevas funcionalidades que no puedan ser
validadas usando las pruebas existentes de KVM y/o las pruebas unitarias de
KVM deben venir con pruebas.

Es más que bienvenido el envío de nuevos desarrollos de características sin
pruebas para obtener un feedback temprano, pero tales envíos deben ser
etiquetados como RFC, y la carta de presentación debe indicar claramente
qué tipo de feedback se solicita/espera. No abuse del proceso de RFC; las
RFC no suelen recibir una revisión en profundidad.

Corrección de Errores
~~~~~~~~~~~~~~~~~~~~~
Salvo en el caso de fallos "obvios" detectados por inspección, las
correcciones deben ir acompañadas de un reproductor del fallo corregido. En
muchos casos, el reproductor está implícito, por ejemplo, para errores de
compilación y fallos de prueba, pero debe quedar claro para lectores qué es
lo que no funciona y cómo verificar la solución. Se concede cierto margen a
los errores detectados mediante cargas de trabajo/pruebas no públicas, pero
se recomienda encarecidamente que se faciliten pruebas de regresión para
dichos errores.

En general, las pruebas de regresión son preferibles para cualquier fallo
que no sea trivial de encontrar. Por ejemplo, incluso si el error fue
encontrado originalmente por un fuzzer como syzkaller, una prueba de
regresión dirigida puede estar justificada si el error requiere golpear una
condición de carrera de tipo uno en un millón.

Recuerde que los fallos de KVM rara vez son urgentes *y* no triviales de
reproducir. Pregúntate si un fallo es realmente el fin del mundo antes de
publicar una corrección sin un reproductor.

Publicación
-----------

Enlaces
~~~~~~~
No haga referencia explícita a informes de errores, versiones anteriores de
un parche/serie, etc. mediante cabeceras ``In-Reply-To:``. Usar
``In-Reply-To:`` se convierte en un lío para grandes series y/o cuando el
número de versiones es alto, y ``In-Reply-To:`` es inútil para cualquiera
que no tenga el mensaje original, por ejemplo, si alguien no recibió un Cc
en el informe de error o si la lista de destinatarios cambia entre
versiones.

Para enlazar con un informe de error, una versión anterior o cualquier cosa
de interés, utiliza enlaces lore. Para hacer referencia a versiones
anteriores, en general no incluya un Enlace: en el registro de cambios, ya
que no hay necesidad de registrar la historia en git, es decir, ponga el
enlace en la carta de presentación o en la sección que git ignora.
Proporcione un Enlace: formal para los informes de errores y/o discusiones
que condujeron al parche. El contexto de por qué se hizo un cambio es muy
valioso para futuros lectores.

Basado en Git
~~~~~~~~~~~~~
Si utilizas la versión 2.9.0 o posterior de git (Googlers, ¡os incluimos a
todos!), utilice ``git format-patch`` con el indicador ``--base`` para
incluir automáticamente la información del árbol base en los parches
generados.

Tenga en cuenta que ``--base=auto`` funciona como se espera si y sólo si el
upstream de una rama se establece en la rama temática base, por ejemplo,
hará lo incorrecto si su upstream se establece en su repositorio personal
con fines de copia de seguridad. Una solución "automática" alternativa es
derivar los nombres de tus ramas de desarrollo basándose en su KVM x86, e
introdúzcalo en ``--base``. Por ejemplo, ``x86/pmu/mi_nombre_de_rama``, y
luego escribir un pequeño wrapper para extraer ``pmu`` del nombre de la
rama actual para obtener ``--base=x/pmu``, donde ``x`` es el nombre que su
repositorio utiliza para rastrear el remoto KVM x86.

Tests de Co-Publicación
~~~~~~~~~~~~~~~~~~~~~~~
Las autopruebas de KVM asociadas a cambios de KVM, por ejemplo, pruebas de
regresión para correcciones de errores, deben publicarse junto con los
cambios de KVM como una única serie. Se aplicarán las reglas estándar del
núcleo para la bisección, es decir, los cambios de KVM que provoquen fallos
en las pruebas se ordenarán después de las actualizaciones de las
autopruebas, y viceversa. Las pruebas que fallan debido a errores de KVM
deben ordenarse después de las correcciones de KVM.

KVM-unit-tests debería *siempre* publicarse por separado. Las herramientas,
por ejemplo b4 am, no saben que KVM-unit-tests es un repositorio separado y
se confunden cuando los parches de una serie se aplican en diferentes
árboles. Para vincular los parches de KVM-unit-tests a Parches KVM, primero
publique los cambios KVM y luego proporcione un enlace lore Link: al
parche/serie KVM en el parche(s) KVM-unit-tests.

Notificaciones
~~~~~~~~~~~~~~
Cuando se acepte oficialmente un parche/serie, se enviará un correo
electrónico de notificación en respuesta a la publicación original (carta
de presentación para series de varios parches). La notificación incluirá el
árbol y la rama temática, junto con los SHA1 de los commits de los parches
aplicados.

Si se aplica un subconjunto de parches, se indicará claramente en la
notificación. A menos que se indique lo contrario, se sobreentiende que
todos los parches del Las series que no han sido aceptadas necesitan más
trabajo y deben presentarse en una nueva versión.

Si por alguna razón se retira un parche después de haber sido aceptado
oficialmente, se enviará una respuesta al correo electrónico de
notificación explicando por qué se ha retirado el parche, así como los
pasos siguientes.

Estabilidad SHA1
~~~~~~~~~~~~~~~~
Los SHA1 no son 100% estables hasta que llegan al árbol de Linus. Un SHA1
es *normalmente* estable una vez que se ha enviado una notificación, pero
ocurren cosas. En la mayoría de los casos, se proporcionará una
actualización del correo electrónico de notificación si se aplica un SHA1
del parche. Sin embargo, en algunos escenarios, por ejemplo, si todas las
ramas de KVM x86 necesitan ser rebasadas, no se darán notificaciones
individuales.

Vulnerabilidades
~~~~~~~~~~~~~~~~
Los fallos que pueden ser explotados por la VM (el "guest") para atacar al
host (kernel o espacio de usuario), o que pueden ser explotados por una VM
anidada a *su* host (L2 atacando a L1), son de particular interés para KVM.
Por favor, siga el protocolo para :ref:`securitybugs` si sospecha que un
fallo puede provocar una filtración de datos, etc.
