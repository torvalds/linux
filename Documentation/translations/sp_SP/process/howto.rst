.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/howto.rst <process_howto>`
:Translator: Carlos Bilbao <carlos.bilbao@amd.com>

.. _sp_process_howto:

Cómo participar en el desarrollo del kernel de Linux
====================================================

Este documento es el principal punto de partida. Contiene instrucciones
sobre cómo convertirse en desarrollador del kernel de Linux y explica cómo
trabajar con el y en su desarrollo. El documento no tratará ningún aspecto
técnico relacionado con la programación del kernel, pero le ayudará
guiándole por el camino correcto.

Si algo en este documento quedara obsoleto, envíe parches al maintainer de
este archivo, que se encuentra en la parte superior del documento.

Introducción
------------
¿De modo que quiere descubrir como convertirse en un/a desarrollador/a del
kernel de Linux? Tal vez su jefe le haya dicho, "Escriba un driver de
Linux para este dispositivo." El objetivo de este documento en enseñarle
todo cuanto necesita para conseguir esto, describiendo el proceso por el
que debe pasar, y con indicaciones de como trabajar con la comunidad.
También trata de explicar las razones por las cuales la comunidad trabaja
de la forma en que lo hace.

El kernel esta principalmente escrito en C, con algunas partes que son
dependientes de la arquitectura en ensamblador. Un buen conocimiento de C
es necesario para desarrollar en el kernel. Lenguaje ensamblador (en
cualquier arquitectura) no es necesario excepto que planee realizar
desarrollo de bajo nivel para dicha arquitectura. Aunque no es un perfecto
sustituto para una educación sólida en C y/o años de experiencia, los
siguientes libros sirven, como mínimo, como referencia:

- "The C Programming Language" de Kernighan e Ritchie [Prentice Hall]
- "Practical C Programming" de Steve Oualline [O'Reilly]
- "C:  A Reference Manual" de Harbison and Steele [Prentice Hall]

El kernel está escrito usando GNU C y la cadena de herramientas GNU. Si
bien se adhiere al estándar ISO C89, utiliza una serie de extensiones que
no aparecen en dicho estándar. El kernel usa un C independiente de entorno,
sin depender de la biblioteca C estándar, por lo que algunas partes del
estándar C no son compatibles. Divisiones de long long arbitrarios o
de coma flotante no son permitidas. En ocasiones, puede ser difícil de
entender las suposiciones que el kernel hace respecto a la cadena de
herramientas y las extensiones que usa, y desafortunadamente no hay
referencia definitiva para estas. Consulte las páginas de información de
gcc (`info gcc`) para obtener información al respecto.

Recuerde que está tratando de aprender a trabajar con una comunidad de
desarrollo existente. Es un grupo diverso de personas, con altos estándares
de código, estilo y procedimiento. Estas normas han sido creadas a lo
largo del tiempo en función de lo que se ha encontrado que funciona mejor
para un equipo tan grande y geográficamente disperso. Trate de aprender
tanto como le sea posible acerca de estos estándares antes de tiempo, ya
que están bien documentados; no espere que la gente se adapte a usted o a
la forma de hacer las cosas en su empresa.

Cuestiones legales
------------------
El código fuente del kernel de Linux se publica bajo licencia GPL. Por
favor, revise el archivo COPYING, presente en la carpeta principal del
código fuente, para detalles de la licencia. Si tiene alguna otra pregunta
sobre licencias, contacte a un abogado, no pregunte en listas de discusión
del kernel de Linux. La gente en estas listas no son abogadas, y no debe
confiar en sus opiniones en materia legal.

Para preguntas y respuestas más frecuentes sobre la licencia GPL, consulte:

	https://www.gnu.org/licenses/gpl-faq.html

Documentación
--------------
El código fuente del kernel de Linux tiene una gran variedad de documentos
que son increíblemente valiosos para aprender a interactuar con la
comunidad del kernel. Cuando se agregan nuevas funciones al kernel, se
recomienda que se incluyan nuevos archivos de documentación que expliquen
cómo usar la función. Cuando un cambio en el kernel hace que la interfaz
que el kernel expone espacio de usuario cambie, se recomienda que envíe la
información o un parche en las páginas del manual que expliquen el cambio
a mtk.manpages@gmail.com, y CC la lista linux-api@vger.kernel.org.

Esta es la lista de archivos que están en el código fuente del kernel y son
de obligada lectura:

  :ref:`Documentation/admin-guide/README.rst <readme>`
    Este archivo ofrece una breve descripción del kernel de Linux y
    describe lo que es necesario hacer para configurar y compilar el
    kernel. Quienes sean nuevos en el kernel deben comenzar aquí.

  :ref:`Documentation/process/changes.rst <changes>`
    Este archivo proporciona una lista de los niveles mínimos de varios
    paquetes que son necesarios para construir y ejecutar el kernel
    exitosamente.

  :ref:`Documentation/process/coding-style.rst <codingstyle>`
    Esto describe el estilo de código del kernel de Linux y algunas de los
    razones detrás de esto. Se espera que todo el código nuevo siga las
    directrices de este documento. La mayoría de los maintainers solo
    aceptarán parches si se siguen estas reglas, y muchas personas solo
    revisan el código si tiene el estilo adecuado.

  :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
    Este archivo describe en gran detalle cómo crear con éxito y enviar un
    parche, que incluye (pero no se limita a):

       - Contenidos del correo electrónico (email)
       - Formato del email
       - A quien se debe enviar

    Seguir estas reglas no garantiza el éxito (ya que todos los parches son
    sujetos a escrutinio de contenido y estilo), pero en caso de no seguir
    dichas reglas, el fracaso es prácticamente garantizado.
    Otras excelentes descripciones de cómo crear parches correctamente son:

	"The Perfect Patch"
		https://www.ozlabs.org/~akpm/stuff/tpp.txt

	"Linux kernel patch submission format"
		https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html

  :ref:`Documentation/process/stable-api-nonsense.rst <stable_api_nonsense>`
    Este archivo describe la lógica detrás de la decisión consciente de
    no tener una API estable dentro del kernel, incluidas cosas como:

      - Capas intermedias del subsistema (por compatibilidad?)
      - Portabilidad de drivers entre sistemas operativos
      - Mitigar el cambio rápido dentro del árbol de fuentes del kernel (o
        prevenir cambios rápidos)

     Este documento es crucial para comprender la filosofía del desarrollo
     de Linux y es muy importante para las personas que se mudan a Linux
     tras desarrollar otros sistemas operativos.

  :ref:`Documentation/process/security-bugs.rst <securitybugs>`
    Si cree que ha encontrado un problema de seguridad en el kernel de
    Linux, siga los pasos de este documento para ayudar a notificar a los
    desarrolladores del kernel y ayudar a resolver el problema.

  :ref:`Documentation/process/management-style.rst <managementstyle>`
    Este documento describe cómo operan los maintainers del kernel de Linux
    y los valores compartidos detrás de sus metodologías. Esta es una
    lectura importante para cualquier persona nueva en el desarrollo del
    kernel (o cualquier persona que simplemente sienta curiosidad por
    el campo IT), ya que clarifica muchos conceptos erróneos y confusiones
    comunes sobre el comportamiento único de los maintainers del kernel.

  :ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`
    Este archivo describe las reglas sobre cómo se suceden las versiones
    del kernel estable, y qué hacer si desea obtener un cambio en una de
    estas publicaciones.

  :ref:`Documentation/process/kernel-docs.rst <kernel_docs>`
    Una lista de documentación externa relativa al desarrollo del kernel.
    Por favor consulte esta lista si no encuentra lo que están buscando
    dentro de la documentación del kernel.

  :ref:`Documentation/process/applying-patches.rst <applying_patches>`
    Una buena introducción que describe exactamente qué es un parche y cómo
    aplicarlo a las diferentes ramas de desarrollo del kernel.

El kernel también tiene una gran cantidad de documentos que pueden ser
generados automáticamente desde el propio código fuente o desde
ReStructuredText markups (ReST), como este. Esto incluye un descripción
completa de la API en el kernel y reglas sobre cómo manejar cerrojos
(locking) correctamente.

Todos estos documentos se pueden generar como PDF o HTML ejecutando::

	make pdfdocs
	make htmldocs

respectivamente desde el directorio fuente principal del kernel.

Los documentos que utilizan el markup ReST se generarán en
Documentation/output. También se pueden generar en formatos LaTeX y ePub
con::

	make latexdocs
	make epubdocs

Convertirse en un/a desarrollador/a de kernel
---------------------------------------------

Si no sabe nada sobre el desarrollo del kernel de Linux, debería consultar
el proyecto Linux KernelNewbies:

	https://kernelnewbies.org

Consiste en una útil lista de correo donde puede preguntar casi cualquier
tipo de pregunta básica de desarrollo del kernel (asegúrese de buscar en
los archivos primero, antes de preguntar algo que ya ha sido respondido en
el pasado.) También tiene un canal IRC que puede usar para hacer preguntas
en tiempo real, y una gran cantidad de documentación útil para ir
aprendiendo sobre el desarrollo del kernel de Linux.

El sitio web tiene información básica sobre la organización del código,
subsistemas, y proyectos actuales (tanto dentro como fuera del árbol).
También describe alguna información logística básica, como cómo compilar
un kernel y aplicar un parche.

Si no sabe por dónde quiere empezar, pero quieres buscar alguna tarea que
comenzar a hacer para unirse a la comunidad de desarrollo del kernel,
acuda al proyecto Linux Kernel Janitor:

	https://kernelnewbies.org/KernelJanitors

Es un gran lugar para comenzar. Describe una lista de problemas
relativamente simples que deben limpiarse y corregirse dentro del código
fuente del kernel de Linux árbol de fuentes. Trabajando con los
desarrolladores a cargo de este proyecto, aprenderá los conceptos básicos
para incluir su parche en el árbol del kernel de Linux, y posiblemente
descubrir en la dirección en que trabajar a continuación, si no tiene ya
una idea.

Antes de realizar cualquier modificación real al código del kernel de
Linux, es imperativo entender cómo funciona el código en cuestión. Para
este propósito, nada es mejor que leerlo directamente (lo más complicado
está bien comentado), tal vez incluso con la ayuda de herramientas
especializadas. Una de esas herramientas que se recomienda especialmente
es el proyecto Linux Cross-Reference, que es capaz de presentar el código
fuente en un formato de página web indexada y autorreferencial. Una
excelente puesta al día del repositorio del código del kernel se puede
encontrar en:

	https://elixir.bootlin.com/

El proceso de desarrollo
------------------------

El proceso de desarrollo del kernel de Linux consiste actualmente de
diferentes "branches" (ramas) con muchos distintos subsistemas específicos
a cada una de ellas. Las diferentes ramas son:

  - El código principal de Linus (mainline tree)
  - Varios árboles estables con múltiples major numbers
  - Subsistemas específicos
  - linux-next, para integración y testing

Mainline tree (Árbol principal)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

El mainline tree es mantenido por Linus Torvalds, y puede encontrarse en
https://kernel.org o en su repo.  El proceso de desarrollo es el siguiente:

  - Tan pronto como se lanza un nuevo kernel, se abre una ventana de dos
    semanas, durante este período de tiempo, los maintainers pueden enviar
    grandes modificaciones a Linus, por lo general los parches que ya se
    han incluido en el linux-next durante unas semanas. La forma preferida
    de enviar grandes cambios es usando git (la herramienta de
    administración de código fuente del kernel, más información al respecto
    en https://git-scm.com/), pero los parches simples también son validos.
  - Después de dos semanas, se lanza un kernel -rc1 y la atención se centra
    en hacer el kernel nuevo lo más estable ("solido") posible. La mayoría
    de los parches en este punto deben arreglar una regresión. Los errores
    que siempre han existido no son regresiones, por lo tanto, solo envíe
    este tipo de correcciones si son importantes. Tenga en cuenta que se
    podría aceptar un controlador (o sistema de archivos) completamente
    nuevo después de -rc1 porque no hay riesgo de causar regresiones con
    tal cambio, siempre y cuando el cambio sea autónomo y no afecte áreas
    fuera del código que se está agregando. git se puede usar para enviar
    parches a Linus después de que se lance -rc1, pero los parches también
    deben ser enviado a una lista de correo pública para su revisión.
  - Se lanza un nuevo -rc cada vez que Linus considera que el árbol git
    actual esta en un estado razonablemente sano y adecuado para la prueba.
    La meta es lanzar un nuevo kernel -rc cada semana.
  - El proceso continúa hasta que el kernel se considera "listo", y esto
    puede durar alrededor de 6 semanas.

Vale la pena mencionar lo que Andrew Morton escribió en las listas de
correo del kernel de Linux, sobre lanzamientos del kernel (traducido):

	*"Nadie sabe cuándo se publicara un nuevo kernel, pues esto sucede
	según el estado de los bugs, no de una cronología preconcebida."*

Varios árboles estables con múltiples major numbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Los kernels con versiones de 3 partes son kernels estables. Estos contienen
correcciones relativamente pequeñas y críticas para problemas de seguridad
o importantes regresiones descubiertas para una publicación de código.
Cada lanzamiento en una gran serie estable incrementa la tercera parte de
la versión número, manteniendo las dos primeras partes iguales.

Esta es la rama recomendada para los usuarios que quieren la versión
estable más reciente del kernel, y no están interesados en ayudar a probar
versiones en desarrollo/experimentales.

Los árboles estables son mantenidos por el equipo "estable"
<stable@vger.kernel.org>, y se liberan (publican) según lo dicten las
necesidades. El período de liberación normal es de aproximadamente dos
semanas, pero puede ser más largo si no hay problemas apremiantes. Un
problema relacionado con la seguridad, en cambio, puede causar un
lanzamiento casi instantáneamente.

El archivo :ref:`Documentación/proceso/stable-kernel-rules.rst <stable_kernel_rules>`
en el árbol del kernel documenta qué tipos de cambios son aceptables para
el árbol estable y cómo funciona el proceso de lanzamiento.

Subsistemas específicos
~~~~~~~~~~~~~~~~~~~~~~~~
Los maintainers de los diversos subsistemas del kernel --- y también muchos
desarrolladores de subsistemas del kernel --- exponen su estado actual de
desarrollo en repositorios fuente. De esta manera, otros pueden ver lo que
está sucediendo en las diferentes áreas del kernel. En áreas donde el
desarrollo es rápido, se le puede pedir a un desarrollador que base sus
envíos en tal árbol del subsistema del kernel, para evitar conflictos entre
este y otros trabajos ya en curso.

La mayoría de estos repositorios son árboles git, pero también hay otros
SCM en uso, o colas de parches que se publican como series quilt. Las
direcciones de estos repositorios de subsistemas se enumeran en el archivo
MAINTAINERS. Muchos de estos se pueden ver en https://git.kernel.org/.

Antes de que un parche propuesto se incluya con dicho árbol de subsistemas,
es sujeto a revisión, que ocurre principalmente en las listas de correo
(ver la sección respectiva a continuación). Para varios subsistemas del
kernel, esta revisión se rastrea con la herramienta patchwork. Patchwork
ofrece una interfaz web que muestra publicaciones de parches, cualquier
comentario sobre un parche o revisiones a él, y los maintainers pueden
marcar los parches como en revisión, aceptado, o rechazado. La mayoría de
estos sitios de trabajo de parches se enumeran en

https://patchwork.kernel.org/.

linux-next, para integración y testing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Antes de que las actualizaciones de los árboles de subsistemas se combinen
con el árbol principal, necesitan probar su integración. Para ello, existe
un repositorio especial de pruebas en el que se encuentran casi todos los
árboles de subsistema, actualizado casi a diario:

	https://git.kernel.org/?p=linux/kernel/git/next/linux-next.git

De esta manera, linux-next ofrece una perspectiva resumida de lo que se
espera que entre en el kernel principal en el próximo período de "merge"
(fusión de código). Los testers aventureros son bienvenidos a probar
linux-next en ejecución.

Reportar bugs
-------------

El archivo 'Documentación/admin-guide/reporting-issues.rst' en el
directorio principal del kernel describe cómo informar un posible bug del
kernel y detalles sobre qué tipo de información necesitan los
desarrolladores del kernel para ayudar a rastrear la fuente del problema.

Gestión de informes de bugs
------------------------------

Una de las mejores formas de poner en práctica sus habilidades de hacking
es arreglando errores reportados por otras personas. No solo ayudará a
hacer el kernel más estable, también aprenderá a solucionar problemas del
mundo real y mejora sus habilidades, y otros desarrolladores se darán
cuenta de tu presencia. La corrección de errores es una de las mejores
formas de ganar méritos entre desarrolladores, porque no a muchas personas
les gusta perder el tiempo arreglando los errores de otras personas.

Para trabajar en informes de errores ya reportados, busque un subsistema
que le interese. Verifique el archivo MAINTAINERS donde se informan los
errores de ese subsistema; con frecuencia será una lista de correo, rara
vez un rastreador de errores (bugtracker). Busque en los archivos de dicho
lugar para informes recientes y ayude donde lo crea conveniente. También es
posible que desee revisar https://bugzilla.kernel.org para informes de
errores; solo un puñado de subsistemas del kernel lo emplean activamente
para informar o rastrear; sin embargo, todos los errores para todo el kernel
se archivan allí.

Listas de correo
-----------------

Como se explica en algunos de los documentos anteriores, la mayoría de
desarrolladores del kernel participan en la lista de correo del kernel de
Linux. Detalles sobre cómo para suscribirse y darse de baja de la lista se
pueden encontrar en:

	http://vger.kernel.org/vger-lists.html#linux-kernel

Existen archivos de la lista de correo en la web en muchos lugares
distintos. Utilice un motor de búsqueda para encontrar estos archivos. Por
ejemplo:

	http://dir.gmane.org/gmane.linux.kernel

Es muy recomendable que busque en los archivos sobre el tema que desea
tratar, antes de publicarlo en la lista. Un montón de cosas ya discutidas
en detalle solo se registran en los archivos de la lista de correo.

La mayoría de los subsistemas individuales del kernel también tienen sus
propias lista de correo donde hacen sus esfuerzos de desarrollo. Revise el
archivo MAINTAINERS para obtener referencias de lo que estas listas para
los diferentes grupos.

Muchas de las listas están alojadas en kernel.org. La información sobre
estas puede ser encontrada en:

	http://vger.kernel.org/vger-lists.html

Recuerde mantener buenos hábitos de comportamiento al usar las listas.
Aunque un poco cursi, la siguiente URL tiene algunas pautas simples para
interactuar con la lista (o cualquier lista):

	http://www.albion.com/netiquette/

Si varias personas responden a su correo, el CC (lista de destinatarios)
puede hacerse bastante grande. No elimine a nadie de la lista CC: sin una
buena razón, o no responda solo a la dirección de la lista. Acostúmbrese
a recibir correos dos veces, una del remitente y otra de la lista, y no
intente ajustar esto agregando encabezados de correo astutos, a la gente no
le gustará.

Recuerde mantener intacto el contexto y la atribución de sus respuestas,
mantenga las líneas "El hacker John Kernel escribió ...:" en la parte
superior de su respuesta, y agregue sus declaraciones entre las secciones
individuales citadas en lugar de escribiendo en la parte superior del
correo electrónico.

Si incluye parches en su correo, asegúrese de que sean texto legible sin
formato como se indica en :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`.
Los desarrolladores del kernel no quieren lidiar con archivos adjuntos o
parches comprimidos; y pueden querer comentar líneas individuales de su
parche, que funciona sólo de esa manera. Asegúrese de emplear un programa
de correo que no altere los espacios ni los tabuladores. Una buena primera
prueba es enviarse el correo a usted mismo, e intentar aplicar su
propio parche. Si eso no funciona, arregle su programa de correo o
reemplace hasta que funcione.

Sobretodo, recuerde de ser respetuoso con otros subscriptores.

Colaborando con la comunidad
----------------------------

El objetivo de la comunidad del kernel es proporcionar el mejor kernel
posible. Cuando envíe un parche para su aceptación, se revisará en sus
méritos técnicos solamente. Entonces, ¿qué deberías ser?

  - críticas
  - comentarios
  - peticiones de cambios
  - peticiones de justificaciones
  - silencio

Recuerde, esto es parte de introducir su parche en el kernel. Tiene que ser
capaz de recibir críticas y comentarios sobre sus parches, evaluar
a nivel técnico y re-elaborar sus parches o proporcionar razonamiento claro
y conciso de por qué no se deben hacer tales cambios. Si no hay respuestas
a su publicación, espere unos días e intente de nuevo, a veces las cosas se
pierden dado el gran volumen.

¿Qué no debería hacer?

  - esperar que su parche se acepte sin preguntas
  - actuar de forma defensiva
  - ignorar comentarios
  - enviar el parche de nuevo, sin haber aplicados los cambios pertinentes

En una comunidad que busca la mejor solución técnica posible, siempre habrá
diferentes opiniones sobre lo beneficioso que es un parche. Tiene que ser
cooperativo y estar dispuesto a adaptar su idea para que encaje dentro
del kernel, o al menos esté dispuesto a demostrar que su idea vale la pena.
Recuerde, estar equivocado es aceptable siempre y cuando estés dispuesto a
trabajar hacia una solución que sea correcta.

Es normal que las respuestas a su primer parche sean simplemente una lista
de una docena de cosas que debe corregir. Esto **no** implica que su
parche no será aceptado, y **no** es personal. Simplemente corrija todos
los problemas planteados en su parche, y envié otra vez.

Diferencias entre la comunidad kernel y las estructuras corporativas
--------------------------------------------------------------------

La comunidad del kernel funciona de manera diferente a la mayoría de los
entornos de desarrollo tradicionales en empresas. Aquí hay una lista de
cosas que puede intentar hacer para evitar problemas:

  Cosas buenas que decir respecto a los cambios propuestos:

    - "Esto arregla múltiples problemas."
    - "Esto elimina 2000 lineas de código."
    - "Aquí hay un parche que explica lo que intento describir."
    - "Lo he testeado en 5 arquitecturas distintas..."
    - "Aquí hay una serie de parches menores que..."
    - "Esto mejora el rendimiento en maquinas típicas..."

  Cosas negativas que debe evitar decir:

    - "Lo hicimos así en AIX/ptx/Solaris, de modo que debe ser bueno..."
    - "Llevo haciendo esto 20 años, de modo que..."
    - "Esto lo necesita mi empresa para ganar dinero"
    - "Esto es para la linea de nuestros productos Enterprise"
    - "Aquí esta el documento de 1000 paginas describiendo mi idea"
    - "Llevo 6 meses trabajando en esto..."
    - "Aquí esta un parche de 5000 lineas que..."
    - "He rescrito todo el desastre actual, y aquí esta..."
    - "Tengo un deadline, y este parche debe aplicarse ahora."

Otra forma en que la comunidad del kernel es diferente a la mayoría de los
entornos de trabajo tradicionales en ingeniería de software, es la
naturaleza sin rostro de interacción. Una de las ventajas de utilizar el
correo electrónico y el IRC como formas principales de comunicación es la
no discriminación por motivos de género o raza. El entorno de trabajo del
kernel de Linux acepta a mujeres y minorías porque todo lo que eres es una
dirección de correo electrónico. El aspecto internacional también ayuda a
nivelar el campo de juego porque no puede adivinar el género basado en
el nombre de una persona. Un hombre puede llamarse Andrea y una mujer puede
llamarse Pat. La mayoría de las mujeres que han trabajado en el kernel de
Linux y han expresado una opinión han tenido experiencias positivas.

La barrera del idioma puede causar problemas a algunas personas que no se
sientes cómodas con el inglés. Un buen dominio del idioma puede ser
necesario para transmitir ideas correctamente en las listas de correo, por
lo que le recomendamos que revise sus correos electrónicos para asegurarse
de que tengan sentido en inglés antes de enviarlos.

Divida sus cambios
---------------------

La comunidad del kernel de Linux no acepta con gusto grandes fragmentos de
código, sobretodo a la vez. Los cambios deben introducirse correctamente,
discutidos y divididos en pequeñas porciones individuales. Esto es casi
exactamente lo contrario de lo que las empresas están acostumbradas a hacer.
Su propuesta también debe introducirse muy temprano en el proceso de
desarrollo, de modo que pueda recibir comentarios sobre lo que está
haciendo. También deje que la comunidad sienta que está trabajando con
ellos, y no simplemente usándolos como un vertedero para su función. Sin
embargo, no envíe 50 correos electrónicos a una vez a una lista de correo,
su serie de parches debe casi siempre ser más pequeña que eso.

Las razones para dividir las cosas son las siguientes:

1) Los cambios pequeños aumentan la probabilidad de que sus parches sean
   aplicados, ya que no requieren mucho tiempo o esfuerzo para verificar su
   exactitud. Un parche de 5 líneas puede ser aplicado por un maintainer
   con apenas una segunda mirada. Sin embargo, un parche de 500 líneas
   puede tardar horas en ser revisado en términos de corrección (el tiempo
   que toma es exponencialmente proporcional al tamaño del parche, o algo
   así).

   Los parches pequeños también facilitan la depuración cuando algo falla.
   Es mucho más fácil retirar los parches uno por uno que diseccionar un
   parche muy grande después de haber sido aplicado (y roto alguna cosa).

2) Es importante no solo enviar pequeños parches, sino también reescribir
   y simplificar (o simplemente reordenar) los parches antes de enviarlos.

Esta es una analogía del desarrollador del kernel Al Viro (traducida):

	*"Piense en un maestro que califica la tarea de un estudiante de
	matemáticas. El maestro no quiere ver los intentos y errores del
	estudiante antes de que se les ocurriera la solución. Quiere ver la
	respuesta más limpia y elegante. Un buen estudiante lo sabe, y nunca
	presentaría su trabajo intermedio antes de tener la solución final.*

	*Lo mismo ocurre con el desarrollo del kernel. Los maintainers y
	revisores no quieren ver el proceso de pensamiento detrás de la solución
	al problema que se está resolviendo. Quieren ver un solución simple y
	elegante."*

Puede resultar un reto mantener el equilibrio entre presentar una solución
elegante y trabajar junto a la comunidad, discutiendo su trabajo inacabado.
Por lo tanto, es bueno comenzar temprano en el proceso para obtener
"feedback" y mejorar su trabajo, pero también mantenga sus cambios en
pequeños trozos que pueden ser aceptados, incluso cuando toda su labor no
está listo para inclusión en un momento dado.

También tenga en cuenta que no es aceptable enviar parches para su
inclusión que están sin terminar y serán "arreglados más tarde".

Justifique sus cambios
----------------------

Además de dividir sus parches, es muy importante que deje a la comunidad de
Linux sabe por qué deberían agregar este cambio. Nuevas características
debe justificarse como necesarias y útiles.

Documente sus cambios
---------------------

Cuando envíe sus parches, preste especial atención a lo que dice en el
texto de su correo electrónico. Esta información se convertirá en el
ChangeLog del parche, y se conservará para que todos la vean, todo el
tiempo. Debe describir el parche por completo y contener:

  - por qué los cambios son necesarios
  - el diseño general de su propuesta
  - detalles de implementación
  - resultados de sus experimentos

Para obtener más detalles sobre cómo debería quedar todo esto, consulte la
sección ChangeLog del documento:

  "The Perfect Patch"
      https://www.ozlabs.org/~akpm/stuff/tpp.txt

Todas estas cuestiones son a veces son muy difíciles de conseguir. Puede
llevar años perfeccionar estas prácticas (si es que lo hace). Es un proceso
continuo de mejora que requiere mucha paciencia y determinación. Pero no se
rinda, es posible. Muchos lo han hecho antes, y cada uno tuvo que comenzar
exactamente donde está usted ahora.

----------

Gracias a Paolo Ciarrocchi que permitió que la sección "Development Process"
se basara en el texto que había escrito (https://lwn.net/Articles/94386/),
y a Randy Dunlap y Gerrit Huizenga por algunas de la lista de cosas que
debes y no debes decir. También gracias a Pat Mochel, Hanna Linder, Randy
Dunlap, Kay Sievers, Vojtech Pavlik, Jan Kara, Josh Boyer, Kees Cook,
Andrew Morton, Andi Kleen, Vadim Lobanov, Jesper Juhl, Adrian Bunk,
Keri Harris, Frans Pop, David A. Wheeler, Junio Hamano, Michael Kerrisk y
Alex Shepard por su revisión, comentarios y contribuciones. Sin su ayuda,
este documento no hubiera sido posible.

Maintainer: Greg Kroah-Hartman <greg@kroah.com>
